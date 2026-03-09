#include "ionclaw/server/Routes.hpp"

namespace ionclaw
{
namespace server
{

void Routes::handleChannelsList(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp)
{
    nlohmann::json result = nlohmann::json::object();

    for (const auto &[name, ch] : config->channels)
    {
        result[name] = {
            {"enabled", ch.enabled},
            {"credential", ch.credential},
            {"running", ch.enabled},
        };
    }

    sendJson(resp, result);
}

void Routes::handleChannelGet(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp, const std::string &name)
{
    auto it = config->channels.find(name);

    if (it == config->channels.end())
    {
        sendError(resp, "Channel not found", 404);
        return;
    }

    auto &ch = it->second;

    nlohmann::json out = {
        {"name", name},
        {"enabled", ch.enabled},
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
    sendJson(resp, out);
}

void Routes::handleChannelUpdate(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &name)
{
    try
    {
        auto body = nlohmann::json::parse(readBody(req));
        auto configData = body.value("config", nlohmann::json::object());

        // update channel fields from request body
        auto &ch = config->channels[name];

        if (configData.contains("enabled"))
        {
            ch.enabled = configData["enabled"].get<bool>();
        }

        if (configData.contains("credential"))
        {
            ch.credential = configData["credential"].get<std::string>();
        }

        if (configData.contains("allowed_users"))
        {
            ch.allowedUsers.clear();

            for (const auto &u : configData["allowed_users"])
            {
                ch.allowedUsers.push_back(u.get<std::string>());
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

        sendJson(resp, {{"status", "ok"}});
    }
    catch (const std::exception &e)
    {
        sendError(resp, e.what());
    }
}

void Routes::handleChannelStart(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp, const std::string &name)
{
    auto it = config->channels.find(name);

    if (it == config->channels.end())
    {
        sendError(resp, "Channel not found", 404);
        return;
    }

    auto &ch = it->second;

    if (ch.enabled)
    {
        sendError(resp, "Channel already running or not available", 409);
        return;
    }

    try
    {
        channelManager->startChannel(name);
        ch.enabled = true;
        sendJson(resp, {{"status", "started"}});
    }
    catch (const std::exception &e)
    {
        sendError(resp, e.what(), 500);
    }
}

void Routes::handleChannelStop(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp, const std::string &name)
{
    auto it = config->channels.find(name);

    if (it == config->channels.end())
    {
        sendError(resp, "Channel not found", 404);
        return;
    }

    auto &ch = it->second;

    if (!ch.enabled)
    {
        sendError(resp, "Channel not running", 409);
        return;
    }

    channelManager->stopChannel(name);
    ch.enabled = false;
    sendJson(resp, {{"status", "stopped"}});
}

} // namespace server
} // namespace ionclaw
