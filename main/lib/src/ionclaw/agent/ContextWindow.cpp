#include "ionclaw/agent/ContextWindow.hpp"

#include <algorithm>
#include <climits>
#include <cstdint>

#include "ionclaw/util/StringHelper.hpp"

namespace ionclaw
{
namespace agent
{

// model context limits in tokens
const std::map<std::string, int> ContextWindow::MODEL_CONTEXT_LIMITS = {
    // openai
    {"gpt-5", 400000},
    {"gpt-4.1", 1000000},
    {"gpt-4.1-mini", 1000000},
    {"gpt-4.1-nano", 1000000},
    {"gpt-4o", 128000},
    {"gpt-4o-mini", 128000},
    {"gpt-4-turbo", 128000},
    {"o4", 200000},
    {"o4-mini", 200000},
    {"o3", 200000},
    {"o3-mini", 200000},
    // google
    {"gemini-3", 1000000},
    {"gemini-2.5", 1000000},
    {"gemini-2.0", 1000000},
    // anthropic
    {"claude-opus-4", 200000},
    {"claude-sonnet-4", 200000},
    {"claude-3.5", 200000},
    {"claude-3-opus", 200000},
    {"claude-3-sonnet", 200000},
    {"claude-3-haiku", 200000},
    // xai
    {"grok-4", 256000},
    {"grok-3", 128000},
    // moonshot/kimi
    {"kimi-k2", 128000},
    {"kimi-k1", 32000},
    {"moonshot-v1-128k", 131072},
    {"moonshot-v1-32k", 32768},
    {"moonshot-v1-8k", 8192},
    // deepseek
    {"deepseek-r1", 164000},
    {"deepseek-v3", 128000},
    {"deepseek-chat", 64000},
};

// resolve context window limit for the given model
int ContextWindow::getModelLimit(const std::string &model, const nlohmann::json &modelParams, int contextTokensCap)
{
    int limit = DEFAULT_LIMIT;

    // explicit override from model params
    if (modelParams.is_object() && modelParams.contains("context_window") &&
        modelParams["context_window"].is_number())
    {
        limit = modelParams["context_window"].get<int>();
    }
    else
    {
        // lowercase model for substring matching
        std::string modelLower = model;
        ionclaw::util::StringHelper::toLowerInPlace(modelLower);

        // find longest matching model key (most specific match wins)
        size_t bestLen = 0;

        for (const auto &[key, modelLimit] : MODEL_CONTEXT_LIMITS)
        {
            if (modelLower.find(key) != std::string::npos && key.size() > bestLen)
            {
                bestLen = key.size();
                limit = modelLimit;
            }
        }
    }

    // apply global context tokens cap if set and lower
    if (contextTokensCap > 0 && contextTokensCap < limit)
    {
        limit = contextTokensCap;
    }

    return limit;
}

// estimate token count from message character lengths
int ContextWindow::estimateTokens(const std::vector<ionclaw::provider::Message> &messages)
{
    int64_t totalChars = 0;

    for (const auto &msg : messages)
    {
        // count main content
        totalChars += static_cast<int64_t>(msg.content.size());

        // count tool call arguments
        for (const auto &tc : msg.toolCalls)
        {
            totalChars += static_cast<int64_t>(tc.arguments.dump().size());
        }

        // count reasoning content
        totalChars += static_cast<int64_t>(msg.reasoningContent.size());

        // count content blocks
        if (msg.contentBlocks.is_array())
        {
            for (const auto &block : msg.contentBlocks)
            {
                if (block.contains("text") && block["text"].is_string())
                {
                    totalChars += static_cast<int64_t>(block["text"].get<std::string>().size());
                }
            }
        }
    }

    // chars/4 is a conservative estimate, add 20% buffer
    return static_cast<int>(std::min(static_cast<int64_t>(INT32_MAX),
                                     static_cast<int64_t>((totalChars / 4.0) * 1.2)));
}

// trim conversation to the most recent maxHistory messages
std::vector<ionclaw::provider::Message> ContextWindow::trimHistory(const std::vector<ionclaw::provider::Message> &messages, int maxHistory)
{
    if (maxHistory <= 0)
    {
        return messages;
    }

    // separate system messages from conversation
    std::vector<ionclaw::provider::Message> systemMsgs;
    std::vector<ionclaw::provider::Message> conversation;

    for (const auto &msg : messages)
    {
        if (msg.role == "system")
        {
            systemMsgs.push_back(msg);
        }
        else
        {
            conversation.push_back(msg);
        }
    }

    if (static_cast<int>(conversation.size()) <= maxHistory)
    {
        return messages;
    }

    // keep last maxHistory messages
    conversation = std::vector<ionclaw::provider::Message>(
        conversation.end() - maxHistory, conversation.end());

    // skip orphaned tool results at the start
    while (!conversation.empty() && conversation.front().role == "tool")
    {
        conversation.erase(conversation.begin());
    }

    // reassemble with system messages first
    std::vector<ionclaw::provider::Message> result;
    result.reserve(systemMsgs.size() + conversation.size());
    result.insert(result.end(), systemMsgs.begin(), systemMsgs.end());
    result.insert(result.end(), conversation.begin(), conversation.end());
    return result;
}

// check if estimated tokens exceed safety margin of model limit
bool ContextWindow::needsCompaction(const std::vector<ionclaw::provider::Message> &messages, const std::string &model, const nlohmann::json &modelParams, int contextTokensCap)
{
    auto limit = getModelLimit(model, modelParams, contextTokensCap);
    auto estimated = estimateTokens(messages);
    return estimated > static_cast<int>(limit * SAFETY_MARGIN);
}

// check if remaining context window is critically low
ContextGuardResult ContextWindow::checkMinContext(
    const std::vector<ionclaw::provider::Message> &messages,
    const std::string &model,
    const nlohmann::json &modelParams,
    int contextTokensCap)
{
    ContextGuardResult result;
    result.modelLimit = getModelLimit(model, modelParams, contextTokensCap);
    result.estimatedTokens = estimateTokens(messages);

    if (result.modelLimit <= 0)
    {
        return result;
    }

    result.usageRatio = static_cast<double>(result.estimatedTokens) / result.modelLimit;
    auto remaining = result.modelLimit - result.estimatedTokens;

    if (remaining < MIN_RESPONSE_TOKENS || result.usageRatio >= CRITICAL_THRESHOLD)
    {
        result.level = ContextGuardLevel::Critical;
    }
    else if (result.usageRatio >= WARNING_THRESHOLD)
    {
        result.level = ContextGuardLevel::Warning;
    }

    return result;
}

} // namespace agent
} // namespace ionclaw
