#include "ionclaw/agent/Classifier.hpp"

#include <algorithm>
#include <sstream>

#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace agent
{

Classifier::Classifier(std::shared_ptr<ionclaw::provider::LlmProvider> provider, const ionclaw::config::Config &config)
    : provider(std::move(provider))
    , config(config)
{
}

void Classifier::setActiveAgents(const std::vector<std::string> &agents)
{
    activeAgents = agents;
}

std::string Classifier::classify(
    const std::string &message,
    const std::string &sessionKey,
    const std::vector<ionclaw::session::SessionMessage> &history) const
{
    // use active agents if set, otherwise use all configured agents
    std::map<std::string, ionclaw::config::AgentConfig> agents;

    if (!activeAgents.empty())
    {
        for (const auto &name : activeAgents)
        {
            auto it = config.agents.find(name);

            if (it != config.agents.end())
            {
                agents[name] = it->second;
            }
        }
    }
    else
    {
        agents = config.agents;
    }

    if (agents.empty())
    {
        spdlog::warn("[Classifier] No agents configured");
        return "";
    }

    // if only one agent, return it directly
    if (agents.size() == 1)
    {
        return agents.begin()->first;
    }

    std::string firstAgent = agents.begin()->first;

    // build tool-based classification
    std::ostringstream systemPrompt;
    systemPrompt << "You are a message routing classifier. "
                 << "Given the conversation context, use the 'route' tool to select the best agent.\n\n"
                 << "Available agents:\n";

    for (const auto &[name, agentConfig] : agents)
    {
        systemPrompt << "- " << name;

        if (!agentConfig.description.empty())
        {
            systemPrompt << ": " << agentConfig.description;
        }

        systemPrompt << "\n";
    }

    // build messages with recent history context
    std::vector<ionclaw::provider::Message> messages;

    ionclaw::provider::Message sysMsg;
    sysMsg.role = "system";
    sysMsg.content = systemPrompt.str();
    messages.push_back(sysMsg);

    // include up to 20 recent history messages for routing context
    size_t historyLimit = std::min(history.size(), static_cast<size_t>(20));

    if (historyLimit > 0)
    {
        for (size_t i = history.size() - historyLimit; i < history.size(); ++i)
        {
            const auto &msg = history[i];

            if (msg.role.empty() || msg.content.empty())
            {
                continue;
            }

            ionclaw::provider::Message histMsg;
            histMsg.role = msg.role;
            histMsg.content = msg.content;
            messages.push_back(histMsg);
        }
    }

    // add the current message
    ionclaw::provider::Message userMsg;
    userMsg.role = "user";
    userMsg.content = message;
    messages.push_back(userMsg);

    // define the route tool
    nlohmann::json routeTool = {
        {"name", "route"},
        {"description", "Route the message to the best matching agent"},
        {"parameters", {
                           {"type", "object"},
                           {"properties", {
                                              {"agent_name", {
                                                                 {"type", "string"},
                                                                 {"description", "The name of the agent to route to"},
                                                             }},
                                              {"confidence", {
                                                                 {"type", "number"},
                                                                 {"description", "Confidence score between 0 and 1"},
                                                             }},
                                          }},
                           {"required", nlohmann::json::array({"agent_name"})},
                       }},
    };

    ionclaw::provider::ChatCompletionRequest request;
    request.messages = messages;
    request.tools = {routeTool};
    request.temperature = 0.0;
    request.maxTokens = 128;

    if (!config.classifier.model.empty())
    {
        request.model = config.classifier.model;
    }
    else
    {
        // fall back to the first agent's model when no dedicated classifier model is configured
        request.model = agents.at(firstAgent).model;
    }

    if (request.model.empty())
    {
        spdlog::warn("[Classifier] No model configured for classification, skipping LLM call");
        return firstAgent;
    }

    try
    {
        auto response = provider->chat(request);

        // check tool calls for route result
        for (const auto &tc : response.toolCalls)
        {
            if (tc.name == "route" && tc.arguments.contains("agent_name"))
            {
                auto agentName = tc.arguments["agent_name"].get<std::string>();

                if (agents.find(agentName) != agents.end())
                {
                    auto confidence = tc.arguments.value("confidence", 0.0);
                    spdlog::info("[Classifier] Classified to agent '{}' (confidence: {:.2f})", agentName, confidence);
                    return agentName;
                }

                // try case-insensitive match when exact name fails
                for (const auto &[name, agentCfg] : agents)
                {
                    std::string lowerResult = agentName;
                    std::string lowerName = name;
                    std::transform(lowerResult.begin(), lowerResult.end(), lowerResult.begin(),
                                   [](unsigned char c)
                                   { return c < 0x80 ? static_cast<unsigned char>(std::tolower(c)) : c; });
                    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                                   [](unsigned char c)
                                   { return c < 0x80 ? static_cast<unsigned char>(std::tolower(c)) : c; });

                    if (lowerResult == lowerName)
                    {
                        spdlog::info("[Classifier] Classified to agent '{}' (case-insensitive)", name);
                        return name;
                    }
                }
            }
        }

        // fallback: check if the plain text response matches an agent name
        auto result = response.content;
        result.erase(0, result.find_first_not_of(" \t\n\r"));
        result.erase(result.find_last_not_of(" \t\n\r") + 1);

        if (agents.find(result) != agents.end())
        {
            spdlog::info("[Classifier] Classified to agent '{}' (text fallback)", result);
            return result;
        }

        spdlog::warn("[Classifier] Classification did not produce valid agent, falling back to: {}", firstAgent);
    }
    catch (const std::exception &e)
    {
        spdlog::error("[Classifier] Classification failed: {}", e.what());
    }

    return firstAgent;
}

} // namespace agent
} // namespace ionclaw
