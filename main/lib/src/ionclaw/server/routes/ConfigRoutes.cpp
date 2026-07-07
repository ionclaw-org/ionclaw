#include "ionclaw/server/Routes.hpp"

#include <filesystem>

#include "spdlog/spdlog.h"

#include "ionclaw/config/ConfigLoader.hpp"
#include "ionclaw/tool/builtin/ToolHelper.hpp"
#include "ionclaw/util/StringHelper.hpp"

namespace ionclaw
{
namespace server
{

namespace fs = std::filesystem;

// a raw credential field is treated as secret when its name hints at a key, token, password, or secret
bool Routes::looksLikeSecretField(const std::string &name)
{
    auto lower = name;
    ionclaw::util::StringHelper::toLowerInPlace(lower);

    static const std::vector<std::string> markers = {"key", "secret", "token", "password", "passwd", "credential", "private"};

    for (const auto &marker : markers)
    {
        if (lower.find(marker) != std::string::npos)
        {
            return true;
        }
    }

    return false;
}

void Routes::handleConfigGet(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp)
{
    std::lock_guard<std::mutex> lock(configMutex);
    nlohmann::json result;

    // basic sections
    result["bot"] = {{"name", config->bot.name}, {"description", config->bot.description}};
    result["server"] = {{"host", config->server.host}, {"port", config->server.port}, {"public_url", config->server.publicUrl}, {"credential", config->server.credential}};

    // agents
    nlohmann::json agents = nlohmann::json::object();

    for (const auto &[name, agent] : config->agents)
    {
        // convert absolute workspace to relative for display
        auto relativeWorkspace = ionclaw::tool::builtin::ToolHelper::toRelativePath(agent.workspace, config->projectPath);

        agents[name] = {
            {"model", agent.model},
            {"description", agent.description},
            {"instructions", agent.instructions},
            {"workspace", relativeWorkspace},
            {"tools", agent.tools},
            {"agent_params", {
                                 {"max_iterations", agent.agentParams.maxIterations},
                                 {"max_concurrent", agent.agentParams.maxConcurrent},
                                 {"max_history", agent.agentParams.maxHistory},
                             }},
        };
    }

    result["agents"] = agents;

    // providers (masked)
    nlohmann::json providers = nlohmann::json::object();

    for (const auto &[name, prov] : config->providers)
    {
        providers[name] = {
            {"credential", prov.credential},
            {"base_url", prov.baseUrl},
            {"timeout", prov.timeout},
        };
    }

    result["providers"] = providers;

    // credentials (masked)
    nlohmann::json credentials = nlohmann::json::object();

    for (const auto &[name, cred] : config->credentials)
    {
        nlohmann::json item = {{"type", cred.type}};

        if (!cred.key.empty())
        {
            item["key"] = "****";
        }

        if (!cred.username.empty())
        {
            item["username"] = cred.username;
        }

        if (!cred.password.empty())
        {
            item["password"] = "****";
        }

        if (!cred.token.empty())
        {
            item["token"] = "****";
        }

        // include additional fields from raw, masking any that look like secrets
        static const std::set<std::string> knownFields = {"type", "key", "username", "password", "token"};

        if (cred.raw.is_object())
        {
            for (auto &[fieldName, fieldValue] : cred.raw.items())
            {
                if (knownFields.contains(fieldName))
                {
                    continue;
                }

                if (looksLikeSecretField(fieldName) && fieldValue.is_string() && !fieldValue.get<std::string>().empty())
                {
                    item[fieldName] = "****";
                }
                else
                {
                    item[fieldName] = fieldValue;
                }
            }
        }

        credentials[name] = item;
    }

    result["credentials"] = credentials;
    result["web_client"] = {{"credential", config->webClient.credential}};
    result["image"] = {{"model", config->image.model}, {"aspect_ratio", config->image.aspectRatio}, {"size", config->image.size}};
    result["transcription"] = {{"model", config->transcription.model}};
    result["classifier"] = {{"model", config->classifier.model}};
    result["heartbeat"] = {{"enabled", config->heartbeat.enabled}, {"interval", config->heartbeat.interval}, {"agent", config->heartbeat.agent}};

    // tools section with sub-sections
    result["tools"] = {
        {"restrict_to_workspace", config->tools.restrictToWorkspace},
        {"exec", {{"timeout", config->tools.execTimeout}}},
        {"web_search", {{"credential", config->tools.webSearchCredential}, {"provider", config->tools.webSearchProvider}, {"max_results", config->tools.webSearchMaxResults}}},
    };

    result["storage"] = {
        {"type", config->storage.type},
    };

    // channels section
    nlohmann::json channels = nlohmann::json::object();

    for (const auto &[name, ch] : config->channels)
    {
        nlohmann::json chJson = {
            {"enabled", ch.enabled},
            {"credential", ch.credential},
            {"running", ch.running},
        };

        if (!ch.allowedUsers.empty())
        {
            chJson["allowed_users"] = ch.allowedUsers;
        }

        // include channel-specific fields from raw config
        for (const auto &key : {"require_auth", "proxy", "reply_to_message"})
        {
            if (ch.raw.contains(key))
            {
                chJson[key] = ch.raw[key];
            }
        }

        channels[name] = chJson;
    }

    result["channels"] = channels;

    sendJson(resp, result);
}

void Routes::handleConfigYaml(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp)
{
    std::lock_guard<std::mutex> lock(configMutex);
    // mask sensitive values before serializing to yaml
    auto maskedConfig = *config;

    for (auto &[name, cred] : maskedConfig.credentials)
    {
        if (!cred.key.empty())
        {
            cred.key = "****";
        }

        if (!cred.password.empty())
        {
            cred.password = "****";
        }

        if (!cred.token.empty())
        {
            cred.token = "****";
        }

        // mask any raw field that looks like a secret
        if (cred.raw.is_object())
        {
            for (auto &[fieldName, fieldValue] : cred.raw.items())
            {
                if (looksLikeSecretField(fieldName) && fieldValue.is_string() && !fieldValue.get<std::string>().empty())
                {
                    fieldValue = "****";
                }
            }
        }
    }

    auto yaml = ionclaw::config::ConfigLoader::toYaml(maskedConfig);
    sendJson(resp, {{"yaml", yaml}});
}

void Routes::handleConfigUpdate(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp)
{
    try
    {
        std::lock_guard<std::mutex> lock(configMutex);
        auto body = nlohmann::json::parse(readBody(req));

        if (body.contains("sections"))
        {
            auto &sections = body["sections"];

            if (sections.contains("bot"))
            {
                auto &bot = sections["bot"];

                if (bot.contains("name"))
                {
                    config->bot.name = bot["name"].get<std::string>();
                }

                if (bot.contains("description"))
                {
                    config->bot.description = bot["description"].get<std::string>();
                }
            }
        }

        auto configPath = config->projectPath + "/config.yml";
        ionclaw::config::ConfigLoader::save(*config, configPath);

        sendJson(resp, {{"status", "ok"}});
    }
    catch (const std::exception &e)
    {
        sendError(resp, e.what(), 500);
    }
}

void Routes::handleConfigSection(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &section)
{
    try
    {
        std::lock_guard<std::mutex> lock(configMutex);
        auto body = nlohmann::json::parse(readBody(req));
        auto data = body.value("data", nlohmann::json::object());

        if (section == "bot")
        {
            if (data.contains("name"))
            {
                config->bot.name = data["name"].get<std::string>();
            }

            if (data.contains("description"))
            {
                config->bot.description = data["description"].get<std::string>();
            }
        }
        else if (section == "server")
        {
            if (data.contains("host"))
            {
                config->server.host = data["host"].get<std::string>();
            }

            if (data.contains("port"))
            {
                int port = data["port"].get<int>();

                if (port < 1 || port > 65535)
                {
                    sendError(resp, "Port must be between 1 and 65535");
                    return;
                }

                config->server.port = port;
            }

            if (data.contains("public_url"))
            {
                config->server.publicUrl = data["public_url"].get<std::string>();
            }

            if (data.contains("credential"))
            {
                config->server.credential = data["credential"].get<std::string>();
            }
        }
        else if (section == "image")
        {
            if (data.contains("model"))
            {
                config->image.model = data["model"].get<std::string>();
            }

            if (data.contains("aspect_ratio"))
            {
                config->image.aspectRatio = data["aspect_ratio"].get<std::string>();
            }

            if (data.contains("size"))
            {
                config->image.size = data["size"].get<std::string>();
            }
        }
        else if (section == "transcription")
        {
            if (data.contains("model"))
            {
                config->transcription.model = data["model"].get<std::string>();
            }
        }
        else if (section == "tools")
        {
            if (data.contains("restrict_to_workspace"))
            {
                config->tools.restrictToWorkspace = data["restrict_to_workspace"].get<bool>();
            }

            if (data.contains("exec") && data["exec"].is_object())
            {
                auto &exec = data["exec"];

                if (exec.contains("timeout"))
                {
                    auto timeout = exec["timeout"].get<int>();

                    if (timeout > 0)
                    {
                        config->tools.execTimeout = timeout;
                    }
                }
            }

            if (data.contains("web_search") && data["web_search"].is_object())
            {
                auto &ws = data["web_search"];

                if (ws.contains("provider"))
                {
                    config->tools.webSearchProvider = ws["provider"].get<std::string>();
                }

                if (ws.contains("credential"))
                {
                    config->tools.webSearchCredential = ws["credential"].get<std::string>();
                }

                if (ws.contains("max_results"))
                {
                    auto maxResults = ws["max_results"].get<int>();

                    if (maxResults >= 1 && maxResults <= 20)
                    {
                        config->tools.webSearchMaxResults = maxResults;
                    }
                }
            }
        }
        else if (section == "agents")
        {
            for (auto &[agentName, agentData] : data.items())
            {
                auto &agent = config->agents[agentName];

                if (agentData.contains("model"))
                {
                    agent.model = agentData["model"].get<std::string>();
                }

                if (agentData.contains("description"))
                {
                    agent.description = agentData["description"].get<std::string>();
                }

                if (agentData.contains("instructions"))
                {
                    agent.instructions = agentData["instructions"].get<std::string>();
                }

                if (agentData.contains("workspace"))
                {
                    auto ws = agentData["workspace"].get<std::string>();

                    // strip projectPath prefix to keep it relative
                    if (!config->projectPath.empty() && ws.starts_with(config->projectPath))
                    {
                        ws = ws.substr(config->projectPath.size());

                        if (!ws.empty() && ws.front() == '/')
                        {
                            ws = ws.substr(1);
                        }
                    }

                    // store absolute in memory for runtime use
                    if (fs::path(ws).is_relative())
                    {
                        agent.workspace = config->projectPath + "/" + ws;
                    }
                    else
                    {
                        agent.workspace = ws;
                    }
                }

                if (agentData.contains("tools") && agentData["tools"].is_array())
                {
                    agent.tools.clear();

                    for (const auto &t : agentData["tools"])
                    {
                        agent.tools.push_back(t.get<std::string>());
                    }
                }

                if (agentData.contains("agent_params") && agentData["agent_params"].is_object())
                {
                    auto &params = agentData["agent_params"];

                    if (params.contains("max_iterations"))
                    {
                        agent.agentParams.maxIterations = params["max_iterations"].get<int>();
                    }

                    if (params.contains("max_concurrent"))
                    {
                        agent.agentParams.maxConcurrent = params["max_concurrent"].get<int>();
                    }

                    if (params.contains("max_history"))
                    {
                        agent.agentParams.maxHistory = params["max_history"].get<int>();
                    }
                }
            }
        }
        else if (section == "credentials")
        {
            for (auto &[credName, credData] : data.items())
            {
                auto &cred = config->credentials[credName];

                if (credData.contains("type"))
                {
                    cred.type = credData["type"].get<std::string>();
                }

                if (credData.contains("key"))
                {
                    auto val = credData["key"].get<std::string>();

                    if (!val.empty() && val != "****")
                    {
                        cred.key = val;
                    }
                }

                if (credData.contains("username"))
                {
                    cred.username = credData["username"].get<std::string>();
                }

                if (credData.contains("password"))
                {
                    auto val = credData["password"].get<std::string>();

                    if (!val.empty() && val != "****")
                    {
                        cred.password = val;
                    }
                }

                if (credData.contains("token"))
                {
                    auto val = credData["token"].get<std::string>();

                    if (!val.empty() && val != "****")
                    {
                        cred.token = val;
                    }
                }

                // restore any masked secret field from the current raw so a "****" submission keeps the real value
                if (cred.raw.is_object())
                {
                    for (auto &[fieldName, fieldValue] : credData.items())
                    {
                        if (looksLikeSecretField(fieldName) && fieldValue.is_string() && fieldValue.get<std::string>() == "****" && cred.raw.contains(fieldName))
                        {
                            fieldValue = cred.raw[fieldName];
                        }
                    }
                }

                cred.raw = credData;
            }
        }
        else if (section == "providers")
        {
            for (auto &[provName, provData] : data.items())
            {
                auto &prov = config->providers[provName];

                if (provData.contains("credential"))
                {
                    prov.credential = provData["credential"].get<std::string>();
                }

                if (provData.contains("base_url"))
                {
                    prov.baseUrl = provData["base_url"].get<std::string>();
                }

                if (provData.contains("timeout"))
                {
                    prov.timeout = provData["timeout"].get<int>();
                }
            }
        }
        else if (section == "channels")
        {
            for (auto &[chName, chData] : data.items())
            {
                auto &ch = config->channels[chName];

                if (chData.contains("enabled"))
                {
                    ch.enabled = chData["enabled"].get<bool>();
                }

                if (chData.contains("credential"))
                {
                    ch.credential = chData["credential"].get<std::string>();
                }

                if (chData.contains("allowed_users") && chData["allowed_users"].is_array())
                {
                    ch.allowedUsers.clear();

                    for (const auto &u : chData["allowed_users"])
                    {
                        ch.allowedUsers.push_back(u.get<std::string>());
                    }
                }

                ch.raw = chData;
            }
        }
        else if (section == "web_client")
        {
            if (data.contains("credential"))
            {
                config->webClient.credential = data["credential"].get<std::string>();
            }
        }
        else if (section == "storage")
        {
            if (data.contains("type"))
            {
                config->storage.type = data["type"].get<std::string>();
            }
        }
        else if (section == "classifier")
        {
            if (data.contains("model"))
            {
                config->classifier.model = data["model"].get<std::string>();
            }
        }
        else if (section == "heartbeat")
        {
            if (data.contains("enabled"))
            {
                config->heartbeat.enabled = data["enabled"].get<bool>();
            }

            if (data.contains("interval"))
            {
                config->heartbeat.interval = data["interval"].get<int>();
            }

            if (data.contains("agent"))
            {
                config->heartbeat.agent = data["agent"].get<std::string>();
            }
        }
        else if (section == "advanced")
        {
            if (data.contains("yaml"))
            {
                auto yamlStr = data["yaml"].get<std::string>();

                // validate yaml before writing to disk to prevent file corruption
                auto newConfig = ionclaw::config::ConfigLoader::loadFromString(yamlStr);
                newConfig.projectPath = config->projectPath;

                // restore masked credential values from current config
                for (auto &[name, cred] : newConfig.credentials)
                {
                    auto it = config->credentials.find(name);

                    if (it == config->credentials.end())
                    {
                        continue;
                    }

                    if (cred.key == "****")
                    {
                        cred.key = it->second.key;
                    }

                    if (cred.password == "****")
                    {
                        cred.password = it->second.password;
                    }

                    if (cred.token == "****")
                    {
                        cred.token = it->second.token;
                    }

                    // restore any masked raw field (oauth1, header, etc.) from the existing credential
                    if (cred.raw.is_object() && it->second.raw.is_object())
                    {
                        for (auto &[fieldName, fieldValue] : cred.raw.items())
                        {
                            if (looksLikeSecretField(fieldName) && fieldValue.is_string() && fieldValue.get<std::string>() == "****" && it->second.raw.contains(fieldName))
                            {
                                fieldValue = it->second.raw[fieldName];
                            }
                        }
                    }
                }

                // save with restored credential values (validated config only)
                auto configPath = config->projectPath + "/config.yml";
                ionclaw::config::ConfigLoader::save(newConfig, configPath);

                // resolve relative agent workspaces to absolute so skill and file paths stay correct
                ionclaw::config::ConfigLoader::resolveWorkspaces(newConfig, config->projectPath);
                *config = newConfig;

                sendJson(resp, {{"status", "ok"}});
                return;
            }
        }
        else
        {
            sendError(resp, "Invalid section: " + section);
            return;
        }

        // persist changes to disk
        auto configPath = config->projectPath + "/config.yml";
        ionclaw::config::ConfigLoader::save(*config, configPath);

        sendJson(resp, {{"status", "ok"}});
    }
    catch (const std::exception &e)
    {
        sendError(resp, e.what(), 500);
    }
}

void Routes::handleConfigValidate(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp)
{
    try
    {
        auto body = nlohmann::json::parse(readBody(req));
        auto yamlStr = body.value("yaml", "");

        if (yamlStr.empty())
        {
            sendError(resp, "Missing 'yaml' field");
            return;
        }

        try
        {
            ionclaw::config::ConfigLoader::loadFromString(yamlStr);
        }
        catch (const std::exception &e)
        {
            sendJson(resp, {{"valid", false}, {"error", e.what()}});
            return;
        }

        sendJson(resp, {{"valid", true}});
    }
    catch (const std::exception &e)
    {
        sendError(resp, e.what(), 500);
    }
}

void Routes::handleConfigRestart(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp)
{
    try
    {
        std::lock_guard<std::mutex> lock(configMutex);
        auto configPath = config->projectPath + "/config.yml";

        if (!fs::exists(configPath))
        {
            sendError(resp, "config.yml not found", 500);
            return;
        }

        spdlog::info("[Routes] Reloading config from: {}", configPath);

        auto newConfig = ionclaw::config::ConfigLoader::load(configPath);

        // preserve runtime fields set during initial startup
        auto projectPath = config->projectPath;
        newConfig.projectPath = projectPath;
        newConfig.publicDir = projectPath + "/public";

        // preserve server host/port from running instance
        newConfig.server.host = config->server.host;
        newConfig.server.port = config->server.port;

        // resolve agent workspaces
        ionclaw::config::ConfigLoader::resolveWorkspaces(newConfig, projectPath);

        // update shared config
        *config = newConfig;

        // reload auth credentials
        auth->reload(*config);

        // restart orchestrator (stops agents/providers, recreates with new config)
        orchestrator->restart(*config);

        // restart heartbeat service with updated config
        heartbeatService->restart(config->heartbeat.interval, config->heartbeat.enabled, config->heartbeat.agent);

        // restart cron service (jobs persist to cron_jobs.json, independent of config)
        cronService->stop();
        cronService->start();

        // restart channels with updated config
        channelManager->stopAll();

        for (auto &[chName, ch] : config->channels)
        {
            if (ch.enabled)
            {
                try
                {
                    channelManager->startChannel(chName);
                    ch.running = true;
                }
                catch (const std::exception &chErr)
                {
                    spdlog::warn("[Routes] Failed to restart channel '{}': {}", chName, chErr.what());
                    ch.running = false;
                }
            }
            else
            {
                ch.running = false;
            }
        }

        spdlog::info("[Routes] All services restarted successfully");

        sendJson(resp, {{"status", "restarted"}});
    }
    catch (const std::exception &e)
    {
        spdlog::error("[Routes] Failed: {}", e.what());
        sendError(resp, e.what(), 500);
    }
}

void Routes::handleConfigDeleteItem(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp, const std::string &section, const std::string &name)
{
    try
    {
        std::lock_guard<std::mutex> lock(configMutex);

        bool found = false;

        if (section == "agents")
        {
            found = config->agents.erase(name) > 0;
        }
        else if (section == "credentials")
        {
            found = config->credentials.erase(name) > 0;
        }
        else if (section == "providers")
        {
            found = config->providers.erase(name) > 0;
        }

        if (!found)
        {
            sendError(resp, "Not found: " + name, 404);
            return;
        }

        auto configPath = config->projectPath + "/config.yml";
        ionclaw::config::ConfigLoader::save(*config, configPath);

        sendJson(resp, {{"status", "deleted"}, {"section", section}, {"name", name}});
    }
    catch (const std::exception &e)
    {
        sendError(resp, e.what(), 500);
    }
}

} // namespace server
} // namespace ionclaw
