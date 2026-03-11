#include "ionclaw/config/ConfigLoader.hpp"

#include <cstdlib>
#include <fstream>
#include <regex>
#include <stdexcept>

#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace config
{

// environment variable expansion

std::string ConfigLoader::expandEnvVars(const std::string &value)
{
    // pattern for ${VAR_NAME} environment variable references
    static thread_local const std::regex envPattern(R"(\$\{([^}]+)\})");

    std::string result = value;
    std::smatch match;
    int iterations = 0;
    static constexpr int MAX_EXPANSION_ITERATIONS = 20;

    while (std::regex_search(result, match, envPattern) && iterations < MAX_EXPANSION_ITERATIONS)
    {
        const char *envValue = std::getenv(match[1].str().c_str());
        std::string replacement = envValue ? envValue : "";
        result = match.prefix().str() + replacement + match.suffix().str();
        ++iterations;
    }

    if (iterations >= MAX_EXPANSION_ITERATIONS)
    {
        spdlog::warn("[ConfigLoader] Environment variable expansion hit iteration limit for '{}'", value);
    }

    return result;
}

// yaml node expansion helpers

std::string ConfigLoader::expandStr(const YAML::Node &node, const std::string &defaultValue)
{
    if (!node || !node.IsScalar())
    {
        return defaultValue;
    }

    return expandEnvVars(node.as<std::string>());
}

int ConfigLoader::expandInt(const YAML::Node &node, int defaultValue)
{
    if (!node || !node.IsScalar())
    {
        return defaultValue;
    }

    try
    {
        return node.as<int>();
    }
    catch (const YAML::BadConversion &)
    {
        return defaultValue;
    }
}

bool ConfigLoader::expandBool(const YAML::Node &node, bool defaultValue)
{
    if (!node || !node.IsScalar())
    {
        return defaultValue;
    }

    try
    {
        return node.as<bool>();
    }
    catch (const YAML::BadConversion &)
    {
        return defaultValue;
    }
}

std::vector<std::string> ConfigLoader::expandStringList(const YAML::Node &node)
{
    std::vector<std::string> result;

    if (!node || !node.IsSequence())
    {
        return result;
    }

    for (const auto &item : node)
    {
        result.push_back(expandEnvVars(item.as<std::string>()));
    }

    return result;
}

std::map<std::string, std::string> ConfigLoader::expandStringMap(const YAML::Node &node)
{
    std::map<std::string, std::string> result;

    if (!node || !node.IsMap())
    {
        return result;
    }

    for (const auto &pair : node)
    {
        result[pair.first.as<std::string>()] = expandEnvVars(pair.second.as<std::string>());
    }

    return result;
}

// yaml-json conversion helpers

nlohmann::json ConfigLoader::yamlToJson(const YAML::Node &node)
{
    if (!node || node.IsNull())
    {
        return nlohmann::json();
    }

    if (node.IsScalar())
    {
        try
        {
            return node.as<int>();
        }
        catch (...)
        {
        }

        try
        {
            return node.as<double>();
        }
        catch (...)
        {
        }

        try
        {
            return node.as<bool>();
        }
        catch (...)
        {
        }

        return expandEnvVars(node.as<std::string>());
    }

    if (node.IsSequence())
    {
        auto arr = nlohmann::json::array();

        for (const auto &item : node)
        {
            arr.push_back(yamlToJson(item));
        }

        return arr;
    }

    if (node.IsMap())
    {
        auto obj = nlohmann::json::object();

        for (const auto &pair : node)
        {
            obj[pair.first.as<std::string>()] = yamlToJson(pair.second);
        }

        return obj;
    }

    return nlohmann::json();
}

void ConfigLoader::emitJson(YAML::Emitter &out, const nlohmann::json &j)
{
    if (j.is_null())
    {
        out << YAML::Null;
    }
    else if (j.is_boolean())
    {
        out << j.get<bool>();
    }
    else if (j.is_number_integer())
    {
        out << j.get<int64_t>();
    }
    else if (j.is_number_float())
    {
        out << j.get<double>();
    }
    else if (j.is_string())
    {
        out << j.get<std::string>();
    }
    else if (j.is_array())
    {
        out << YAML::BeginSeq;

        for (const auto &item : j)
        {
            emitJson(out, item);
        }

        out << YAML::EndSeq;
    }
    else if (j.is_object())
    {
        out << YAML::BeginMap;

        for (auto it = j.begin(); it != j.end(); ++it)
        {
            out << YAML::Key << it.key() << YAML::Value;
            emitJson(out, it.value());
        }

        out << YAML::EndMap;
    }
}

// load config from yaml file

Config ConfigLoader::load(const std::string &path)
{
    YAML::Node root;

    try
    {
        root = YAML::LoadFile(path);
    }
    catch (const YAML::Exception &e)
    {
        throw std::runtime_error("Failed to load config file: " + path + " - " + e.what());
    }

    Config config;

    // bot
    if (auto bot = root["bot"])
    {
        config.bot.name = expandStr(bot["name"], config.bot.name);
        config.bot.description = expandStr(bot["description"]);
    }

    // server
    if (auto server = root["server"])
    {
        config.server.host = expandStr(server["host"], config.server.host);
        config.server.port = expandInt(server["port"], config.server.port);
        config.server.publicUrl = expandStr(server["public_url"]);
        config.server.credential = expandStr(server["credential"], config.server.credential);
    }

    // web_client
    if (auto webClient = root["web_client"])
    {
        config.webClient.credential = expandStr(webClient["credential"], config.webClient.credential);
    }

    // classifier
    if (auto classifier = root["classifier"])
    {
        config.classifier.model = expandStr(classifier["model"]);
    }

    // image
    if (auto image = root["image"])
    {
        config.image.model = expandStr(image["model"]);
        config.image.aspectRatio = expandStr(image["aspect_ratio"]);
        config.image.size = expandStr(image["size"]);
    }

    // transcription
    if (auto transcription = root["transcription"])
    {
        config.transcription.model = expandStr(transcription["model"]);
    }

    // tools
    if (auto tools = root["tools"])
    {
        config.tools.execTimeout = expandInt(tools["exec_timeout"], config.tools.execTimeout);
        config.tools.webSearchProvider = expandStr(tools["web_search_provider"]);
        config.tools.webSearchCredential = expandStr(tools["web_search_credential"]);

        if (auto ws = tools["web_search"])
        {
            if (ws["provider"])
            {
                config.tools.webSearchProvider = expandStr(ws["provider"]);
            }

            if (ws["credential"])
            {
                config.tools.webSearchCredential = expandStr(ws["credential"]);
            }
        }
    }

    // heartbeat
    if (auto heartbeat = root["heartbeat"])
    {
        config.heartbeat.enabled = expandBool(heartbeat["enabled"], config.heartbeat.enabled);
        config.heartbeat.interval = expandInt(heartbeat["interval"], config.heartbeat.interval);
    }

    // session budget
    if (auto sb = root["session_budget"])
    {
        if (sb["max_disk_bytes"] && sb["max_disk_bytes"].IsScalar())
        {
            try
            {
                config.sessionBudget.maxDiskBytes = sb["max_disk_bytes"].as<int64_t>();
            }
            catch (...)
            {
                config.sessionBudget.maxDiskBytes = 0;
            }
        }

        if (sb["high_water_ratio"] && sb["high_water_ratio"].IsScalar())
        {
            try
            {
                config.sessionBudget.highWaterRatio = sb["high_water_ratio"].as<double>();
            }
            catch (...)
            {
            }
        }
    }

    // messages
    if (auto messages = root["messages"])
    {
        if (auto queue = messages["queue"])
        {
            config.messages.queue.mode = expandStr(queue["mode"], config.messages.queue.mode);
            config.messages.queue.debounceMs = expandInt(queue["debounce_ms"], config.messages.queue.debounceMs);
            config.messages.queue.cap = expandInt(queue["cap"], config.messages.queue.cap);
            config.messages.queue.dropPolicy = expandStr(queue["drop"], config.messages.queue.dropPolicy);

            if (auto byChannel = queue["by_channel"])
            {
                for (const auto &pair : byChannel)
                {
                    config.messages.queue.byChannel[pair.first.as<std::string>()] =
                        expandStr(pair.second);
                }
            }
        }
    }

    // storage
    if (auto storage = root["storage"])
    {
        config.storage.type = expandStr(storage["type"], config.storage.type);
    }

    // agents
    if (auto agents = root["agents"])
    {
        for (const auto &pair : agents)
        {
            std::string name = pair.first.as<std::string>();
            auto node = pair.second;
            AgentConfig agent;

            agent.model = expandStr(node["model"], agent.model);
            agent.description = expandStr(node["description"]);
            agent.instructions = expandStr(node["instructions"]);
            agent.workspace = expandStr(node["workspace"]);
            agent.tools = expandStringList(node["tools"]);

            if (auto params = node["agent_params"])
            {
                agent.agentParams.maxIterations = expandInt(params["max_iterations"], agent.agentParams.maxIterations);
                agent.agentParams.memoryWindow = expandInt(params["memory_window"], agent.agentParams.memoryWindow);
                agent.agentParams.maxConcurrent = expandInt(params["max_concurrent"], agent.agentParams.maxConcurrent);
                agent.agentParams.maxHistory = expandInt(params["max_history"], agent.agentParams.maxHistory);
                agent.agentParams.contextTokens = expandInt(params["context_tokens"], agent.agentParams.contextTokens);

                if (auto chl = params["channel_history_limits"])
                {
                    for (auto it = chl.begin(); it != chl.end(); ++it)
                    {
                        try
                        {
                            agent.agentParams.channelHistoryLimits[it->first.as<std::string>()] =
                                it->second.as<int>();
                        }
                        catch (const YAML::BadConversion &)
                        {
                            spdlog::warn("[ConfigLoader] Invalid channel_history_limits value, skipping");
                        }
                    }
                }
            }

            agent.modelParams = yamlToJson(node["model_params"]);

            // failover profiles
            if (auto profiles = node["profiles"])
            {
                for (const auto &pNode : profiles)
                {
                    ProfileConfig profile;
                    profile.model = expandStr(pNode["model"]);
                    profile.credential = expandStr(pNode["credential"]);
                    profile.baseUrl = expandStr(pNode["base_url"]);
                    profile.priority = expandInt(pNode["priority"], 0);
                    profile.modelParams = yamlToJson(pNode["model_params"]);
                    agent.profiles.push_back(std::move(profile));
                }
            }

            // subagent limits
            if (auto sub = node["subagent_limits"])
            {
                agent.subagentLimits.maxDepth = expandInt(sub["max_depth"], agent.subagentLimits.maxDepth);
                agent.subagentLimits.maxChildren = expandInt(sub["max_children"], agent.subagentLimits.maxChildren);
                agent.subagentLimits.defaultTimeoutSeconds = expandInt(sub["default_timeout_seconds"], agent.subagentLimits.defaultTimeoutSeconds);
                agent.subagentLimits.allowAgents = expandStringList(sub["allow_agents"]);
            }

            // tool policy (allow/deny lists)
            if (auto tp = node["tool_policy"])
            {
                agent.toolPolicy.allow = expandStringList(tp["allow"]);
                agent.toolPolicy.deny = expandStringList(tp["deny"]);
            }

            config.agents[name] = std::move(agent);
        }
    }

    // credentials
    if (auto credentials = root["credentials"])
    {
        for (const auto &pair : credentials)
        {
            std::string name = pair.first.as<std::string>();
            auto node = pair.second;
            CredentialConfig cred;

            cred.type = expandStr(node["type"], cred.type);
            cred.key = expandStr(node["key"]);
            cred.username = expandStr(node["username"]);
            cred.password = expandStr(node["password"]);
            cred.token = expandStr(node["token"]);
            cred.raw = yamlToJson(node);

            config.credentials[name] = std::move(cred);
        }
    }

    // providers
    if (auto providers = root["providers"])
    {
        for (const auto &pair : providers)
        {
            std::string name = pair.first.as<std::string>();
            auto node = pair.second;
            ProviderConfig provider;

            provider.name = name;
            provider.credential = expandStr(node["credential"]);
            provider.baseUrl = expandStr(node["base_url"]);
            provider.timeout = expandInt(node["timeout"], provider.timeout);
            provider.requestHeaders = expandStringMap(node["request_headers"]);
            provider.modelParams = yamlToJson(node["model_params"]);

            config.providers[name] = std::move(provider);
        }
    }

    // channels
    if (auto channels = root["channels"])
    {
        for (const auto &pair : channels)
        {
            std::string name = pair.first.as<std::string>();
            auto node = pair.second;
            ChannelConfig channel;

            channel.enabled = expandBool(node["enabled"], channel.enabled);
            channel.credential = expandStr(node["credential"]);
            channel.allowedUsers = expandStringList(node["allowed_users"]);
            channel.raw = yamlToJson(node);

            config.channels[name] = std::move(channel);
        }
    }

    // forms
    config.forms = yamlToJson(root["forms"]);

    return config;
}

// serialize config to yaml string

std::string ConfigLoader::toYaml(const Config &config)
{
    YAML::Emitter out;
    out << YAML::BeginMap;

    // bot
    out << YAML::Key << "bot" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "name" << YAML::Value << config.bot.name;
    out << YAML::Key << "description" << YAML::Value << config.bot.description;
    out << YAML::EndMap;

    // server
    out << YAML::Key << "server" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "host" << YAML::Value << config.server.host;
    out << YAML::Key << "port" << YAML::Value << config.server.port;
    out << YAML::Key << "public_url" << YAML::Value << config.server.publicUrl;
    out << YAML::Key << "credential" << YAML::Value << config.server.credential;
    out << YAML::EndMap;

    // web_client
    out << YAML::Key << "web_client" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "credential" << YAML::Value << config.webClient.credential;
    out << YAML::EndMap;

    // classifier
    if (!config.classifier.model.empty())
    {
        out << YAML::Key << "classifier" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "model" << YAML::Value << config.classifier.model;
        out << YAML::EndMap;
    }

    // image
    if (!config.image.model.empty() || !config.image.aspectRatio.empty() || !config.image.size.empty())
    {
        out << YAML::Key << "image" << YAML::Value << YAML::BeginMap;

        if (!config.image.model.empty())
        {
            out << YAML::Key << "model" << YAML::Value << config.image.model;
        }

        if (!config.image.aspectRatio.empty())
        {
            out << YAML::Key << "aspect_ratio" << YAML::Value << config.image.aspectRatio;
        }

        if (!config.image.size.empty())
        {
            out << YAML::Key << "size" << YAML::Value << config.image.size;
        }

        out << YAML::EndMap;
    }

    // transcription
    if (!config.transcription.model.empty())
    {
        out << YAML::Key << "transcription" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "model" << YAML::Value << config.transcription.model;
        out << YAML::EndMap;
    }

    // heartbeat
    out << YAML::Key << "heartbeat" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "enabled" << YAML::Value << config.heartbeat.enabled;
    out << YAML::Key << "interval" << YAML::Value << config.heartbeat.interval;
    out << YAML::EndMap;

    // session budget
    if (config.sessionBudget.maxDiskBytes > 0)
    {
        out << YAML::Key << "session_budget" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "max_disk_bytes" << YAML::Value << config.sessionBudget.maxDiskBytes;
        out << YAML::Key << "high_water_ratio" << YAML::Value << config.sessionBudget.highWaterRatio;
        out << YAML::EndMap;
    }

    // messages
    if (config.messages.queue.mode != "collect" ||
        config.messages.queue.debounceMs != 1000 ||
        config.messages.queue.cap != 20 ||
        config.messages.queue.dropPolicy != "summarize" ||
        !config.messages.queue.byChannel.empty())
    {
        out << YAML::Key << "messages" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "queue" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "mode" << YAML::Value << config.messages.queue.mode;
        out << YAML::Key << "debounce_ms" << YAML::Value << config.messages.queue.debounceMs;
        out << YAML::Key << "cap" << YAML::Value << config.messages.queue.cap;
        out << YAML::Key << "drop" << YAML::Value << config.messages.queue.dropPolicy;

        if (!config.messages.queue.byChannel.empty())
        {
            out << YAML::Key << "by_channel" << YAML::Value << YAML::BeginMap;

            for (const auto &[ch, mode] : config.messages.queue.byChannel)
            {
                out << YAML::Key << ch << YAML::Value << mode;
            }

            out << YAML::EndMap;
        }

        out << YAML::EndMap;
        out << YAML::EndMap;
    }

    // agents
    if (!config.agents.empty())
    {
        out << YAML::Key << "agents" << YAML::Value << YAML::BeginMap;

        for (const auto &[name, agent] : config.agents)
        {
            out << YAML::Key << name << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "model" << YAML::Value << agent.model;
            out << YAML::Key << "description" << YAML::Value << agent.description;
            out << YAML::Key << "instructions" << YAML::Value << agent.instructions;
            // save workspace as relative path to avoid absolute paths in config
            auto ws = agent.workspace;

            if (!config.projectPath.empty() && ws.rfind(config.projectPath, 0) == 0)
            {
                ws = ws.substr(config.projectPath.size());

                if (!ws.empty() && ws.front() == '/')
                {
                    ws = ws.substr(1);
                }
            }

            out << YAML::Key << "workspace" << YAML::Value << ws;

            if (!agent.tools.empty())
            {
                out << YAML::Key << "tools" << YAML::Value << YAML::BeginSeq;

                for (const auto &tool : agent.tools)
                {
                    out << tool;
                }

                out << YAML::EndSeq;
            }

            out << YAML::Key << "agent_params" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "max_iterations" << YAML::Value << agent.agentParams.maxIterations;
            out << YAML::Key << "memory_window" << YAML::Value << agent.agentParams.memoryWindow;
            out << YAML::Key << "max_concurrent" << YAML::Value << agent.agentParams.maxConcurrent;
            out << YAML::Key << "max_history" << YAML::Value << agent.agentParams.maxHistory;

            if (agent.agentParams.contextTokens > 0)
            {
                out << YAML::Key << "context_tokens" << YAML::Value << agent.agentParams.contextTokens;
            }

            if (!agent.agentParams.channelHistoryLimits.empty())
            {
                out << YAML::Key << "channel_history_limits" << YAML::Value << YAML::BeginMap;

                for (const auto &[ch, limit] : agent.agentParams.channelHistoryLimits)
                {
                    out << YAML::Key << ch << YAML::Value << limit;
                }

                out << YAML::EndMap;
            }

            out << YAML::EndMap;

            if (!agent.modelParams.is_null() && agent.modelParams.is_object() && !agent.modelParams.empty())
            {
                out << YAML::Key << "model_params" << YAML::Value;
                emitJson(out, agent.modelParams);
            }

            // failover profiles
            if (!agent.profiles.empty())
            {
                out << YAML::Key << "profiles" << YAML::Value << YAML::BeginSeq;

                for (const auto &p : agent.profiles)
                {
                    out << YAML::BeginMap;
                    out << YAML::Key << "model" << YAML::Value << p.model;

                    if (!p.credential.empty())
                    {
                        out << YAML::Key << "credential" << YAML::Value << p.credential;
                    }

                    if (!p.baseUrl.empty())
                    {
                        out << YAML::Key << "base_url" << YAML::Value << p.baseUrl;
                    }

                    if (p.priority != 0)
                    {
                        out << YAML::Key << "priority" << YAML::Value << p.priority;
                    }

                    if (!p.modelParams.is_null() && p.modelParams.is_object() && !p.modelParams.empty())
                    {
                        out << YAML::Key << "model_params" << YAML::Value;
                        emitJson(out, p.modelParams);
                    }

                    out << YAML::EndMap;
                }

                out << YAML::EndSeq;
            }

            // subagent limits
            if (agent.subagentLimits.maxDepth != 5 || agent.subagentLimits.maxChildren != 5 ||
                agent.subagentLimits.defaultTimeoutSeconds > 0 ||
                !agent.subagentLimits.allowAgents.empty())
            {
                out << YAML::Key << "subagent_limits" << YAML::Value << YAML::BeginMap;
                out << YAML::Key << "max_depth" << YAML::Value << agent.subagentLimits.maxDepth;
                out << YAML::Key << "max_children" << YAML::Value << agent.subagentLimits.maxChildren;

                if (agent.subagentLimits.defaultTimeoutSeconds > 0)
                {
                    out << YAML::Key << "default_timeout_seconds" << YAML::Value << agent.subagentLimits.defaultTimeoutSeconds;
                }

                if (!agent.subagentLimits.allowAgents.empty())
                {
                    out << YAML::Key << "allow_agents" << YAML::Value << YAML::BeginSeq;

                    for (const auto &a : agent.subagentLimits.allowAgents)
                    {
                        out << a;
                    }

                    out << YAML::EndSeq;
                }

                out << YAML::EndMap;
            }

            // tool policy
            if (!agent.toolPolicy.allow.empty() || !agent.toolPolicy.deny.empty())
            {
                out << YAML::Key << "tool_policy" << YAML::Value << YAML::BeginMap;

                if (!agent.toolPolicy.allow.empty())
                {
                    out << YAML::Key << "allow" << YAML::Value << YAML::BeginSeq;
                    for (const auto &t : agent.toolPolicy.allow)
                    {
                        out << t;
                    }
                    out << YAML::EndSeq;
                }

                if (!agent.toolPolicy.deny.empty())
                {
                    out << YAML::Key << "deny" << YAML::Value << YAML::BeginSeq;
                    for (const auto &t : agent.toolPolicy.deny)
                    {
                        out << t;
                    }
                    out << YAML::EndSeq;
                }

                out << YAML::EndMap;
            }

            out << YAML::EndMap;
        }

        out << YAML::EndMap;
    }

    // credentials
    if (!config.credentials.empty())
    {
        out << YAML::Key << "credentials" << YAML::Value << YAML::BeginMap;

        for (const auto &[name, cred] : config.credentials)
        {
            out << YAML::Key << name << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "type" << YAML::Value << cred.type;

            if (!cred.key.empty())
            {
                out << YAML::Key << "key" << YAML::Value << cred.key;
            }

            if (!cred.username.empty())
            {
                out << YAML::Key << "username" << YAML::Value << cred.username;
            }

            if (!cred.password.empty())
            {
                out << YAML::Key << "password" << YAML::Value << cred.password;
            }

            if (!cred.token.empty())
            {
                out << YAML::Key << "token" << YAML::Value << cred.token;
            }

            out << YAML::EndMap;
        }

        out << YAML::EndMap;
    }

    // providers
    if (!config.providers.empty())
    {
        out << YAML::Key << "providers" << YAML::Value << YAML::BeginMap;

        for (const auto &[name, provider] : config.providers)
        {
            out << YAML::Key << name << YAML::Value << YAML::BeginMap;
            if (!provider.credential.empty())
            {
                out << YAML::Key << "credential" << YAML::Value << provider.credential;
            }
            out << YAML::Key << "base_url" << YAML::Value << provider.baseUrl;
            out << YAML::Key << "timeout" << YAML::Value << provider.timeout;

            if (!provider.requestHeaders.empty())
            {
                out << YAML::Key << "request_headers" << YAML::Value << YAML::BeginMap;

                for (const auto &[hKey, hVal] : provider.requestHeaders)
                {
                    out << YAML::Key << hKey << YAML::Value << hVal;
                }

                out << YAML::EndMap;
            }

            if (!provider.modelParams.is_null() && provider.modelParams.is_object() && !provider.modelParams.empty())
            {
                out << YAML::Key << "model_params" << YAML::Value;
                emitJson(out, provider.modelParams);
            }

            out << YAML::EndMap;
        }

        out << YAML::EndMap;
    }

    // channels
    if (!config.channels.empty())
    {
        out << YAML::Key << "channels" << YAML::Value << YAML::BeginMap;

        for (const auto &[name, channel] : config.channels)
        {
            out << YAML::Key << name << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "enabled" << YAML::Value << channel.enabled;
            out << YAML::Key << "credential" << YAML::Value << channel.credential;

            if (!channel.allowedUsers.empty())
            {
                out << YAML::Key << "allowed_users" << YAML::Value << YAML::BeginSeq;

                for (const auto &user : channel.allowedUsers)
                {
                    out << user;
                }

                out << YAML::EndSeq;
            }

            if (channel.raw.contains("proxy") && channel.raw["proxy"].is_string() && !channel.raw["proxy"].get<std::string>().empty())
            {
                out << YAML::Key << "proxy" << YAML::Value << channel.raw["proxy"].get<std::string>();
            }
            if (channel.raw.contains("reply_to_message") && channel.raw["reply_to_message"].is_boolean())
            {
                out << YAML::Key << "reply_to_message" << YAML::Value << channel.raw["reply_to_message"].get<bool>();
            }

            out << YAML::EndMap;
        }

        out << YAML::EndMap;
    }

    // storage
    out << YAML::Key << "storage" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "type" << YAML::Value << config.storage.type;
    out << YAML::EndMap;

    // tools
    out << YAML::Key << "tools" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "exec_timeout" << YAML::Value << config.tools.execTimeout;
    out << YAML::Key << "web_search_provider" << YAML::Value << config.tools.webSearchProvider;
    out << YAML::Key << "web_search_credential" << YAML::Value << config.tools.webSearchCredential;
    out << YAML::EndMap;

    out << YAML::EndMap;

    return out.c_str();
}

// save config to yaml file

void ConfigLoader::save(const Config &config, const std::string &path)
{
    auto yaml = toYaml(config);

    std::ofstream fout(path);

    if (!fout.is_open())
    {
        throw std::runtime_error("Failed to open file for writing: " + path);
    }

    fout << yaml;
    fout.flush();

    if (!fout.good())
    {
        throw std::runtime_error("Failed to write config file: " + path);
    }
}

} // namespace config
} // namespace ionclaw
