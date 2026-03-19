#include "ionclaw/server/Routes.hpp"

#include "ionclaw/config/ConfigLoader.hpp"

namespace ionclaw
{
namespace server
{

void Routes::handleChannelsList(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp)
{
    nlohmann::json result = nlohmann::json::object();

    {
        std::lock_guard<std::mutex> lock(configMutex_);

        for (const auto &[name, ch] : config->channels)
        {
            result[name] = {
                {"enabled", ch.enabled},
                {"credential", ch.credential},
                {"running", ch.running},
            };
        }
    }

    sendJson(resp, result);
}

void Routes::handleChannelGet(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp, const std::string &name)
{
    nlohmann::json out;

    {
        std::lock_guard<std::mutex> lock(configMutex_);
        auto it = config->channels.find(name);

        if (it == config->channels.end())
        {
            sendError(resp, "Channel not found", 404);
            return;
        }

        auto &ch = it->second;

        out = {
            {"name", name},
            {"enabled", ch.enabled},
            {"running", ch.running},
            {"credential", ch.credential},
            {"allowed_users", ch.allowedUsers},
        };
        if (ch.raw.contains("proxy") && ch.raw["proxy"].is_string())
        {
            out["proxy"] = ch.raw["proxy"].get<std::string>();
        }
        if (ch.raw.contains("reply_to_message") && ch.raw["reply_to_message"].is_boolean())
        {
            out["reply_to_message"] = ch.raw["reply_to_message"].get<bool>();
        }
        if (ch.raw.contains("require_auth") && ch.raw["require_auth"].is_boolean())
        {
            out["require_auth"] = ch.raw["require_auth"].get<bool>();
        }
    }

    sendJson(resp, out);
}

void Routes::handleChannelUpdate(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &name)
{
    try
    {
        std::lock_guard<std::mutex> lock(configMutex_);
        auto body = nlohmann::json::parse(readBody(req));
        auto configData = body.value("config", nlohmann::json::object());

        auto it = config->channels.find(name);

        if (it == config->channels.end())
        {
            sendError(resp, "Channel not found", 404);
            return;
        }

        auto &ch = it->second;

        if (configData.contains("enabled"))
        {
            ch.enabled = configData["enabled"].get<bool>();
        }

        if (configData.contains("credential") && configData["credential"].is_string())
        {
            ch.credential = configData["credential"].get<std::string>();
        }

        if (configData.contains("allowed_users") && configData["allowed_users"].is_array())
        {
            ch.allowedUsers.clear();

            for (const auto &u : configData["allowed_users"])
            {
                if (u.is_string())
                {
                    ch.allowedUsers.push_back(u.get<std::string>());
                }
            }
        }

        if (configData.contains("proxy"))
        {
            ch.raw["proxy"] = configData["proxy"].is_string() ? configData["proxy"].get<std::string>() : "";
        }
        if (configData.contains("reply_to_message") && configData["reply_to_message"].is_boolean())
        {
            ch.raw["reply_to_message"] = configData["reply_to_message"].get<bool>();
        }
        if (configData.contains("require_auth") && configData["require_auth"].is_boolean())
        {
            ch.raw["require_auth"] = configData["require_auth"].get<bool>();
        }

        ionclaw::config::ConfigLoader::save(*config, config->projectPath + "/config.yml");

        sendJson(resp, {{"status", "ok"}});
    }
    catch (const std::exception &e)
    {
        sendError(resp, e.what(), 500);
    }
}

void Routes::handleChannelStart(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp, const std::string &name)
{
    try
    {
        std::lock_guard<std::mutex> lock(configMutex_);
        auto it = config->channels.find(name);

        if (it == config->channels.end())
        {
            sendError(resp, "Channel not found", 404);
            return;
        }

        auto &ch = it->second;

        if (ch.running)
        {
            sendError(resp, "Channel already running", 409);
            return;
        }

        channelManager->startChannel(name);
        ch.running = true;
        dispatcher->broadcast("channel:status", {{"name", name}, {"running", true}});
        sendJson(resp, {{"status", "started"}});
    }
    catch (const std::exception &e)
    {
        sendError(resp, e.what(), 500);
    }
}

void Routes::handleChannelStop(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp, const std::string &name)
{
    try
    {
        std::lock_guard<std::mutex> lock(configMutex_);
        auto it = config->channels.find(name);

        if (it == config->channels.end())
        {
            sendError(resp, "Channel not found", 404);
            return;
        }

        auto &ch = it->second;

        if (!ch.running)
        {
            sendError(resp, "Channel not running", 409);
            return;
        }

        channelManager->stopChannel(name);
        ch.running = false;
        dispatcher->broadcast("channel:status", {{"name", name}, {"running", false}});
        sendJson(resp, {{"status", "stopped"}});
    }
    catch (const std::exception &e)
    {
        sendError(resp, e.what(), 500);
    }
}

} // namespace server
} // namespace ionclaw
