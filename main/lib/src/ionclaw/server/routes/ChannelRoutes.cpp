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
        auto config = configStore->snapshot();

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
        auto config = configStore->snapshot();
        auto it = config->channels.find(name);

        if (it == config->channels.end())
        {
            sendError(resp, "Channel not found", 404);
            return;
        }

        const auto &ch = it->second;

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

        // whatsapp string fields; secrets are masked so they are never echoed back to the client
        static const std::vector<std::pair<std::string, bool>> whatsAppFields = {{"instance_id", false}, {"instance_token", true}, {"client_token", true}, {"access_token", true}, {"phone_number_id", false}, {"verify_token", true}, {"app_secret", true}, {"graph_version", false}};

        for (const auto &[key, secret] : whatsAppFields)
        {
            if (ch.raw.contains(key) && ch.raw[key].is_string())
            {
                auto value = ch.raw[key].get<std::string>();
                out[key] = (secret && !value.empty()) ? "****" : value;
            }
        }
    }

    sendJson(resp, out);
}

void Routes::handleChannelUpdate(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &name)
{
    try
    {
        auto body = nlohmann::json::parse(readBody(req));
        auto configData = body.value("config", nlohmann::json::object());

        {
            // the whatsapp channels can be created from the api on first configuration; other unknown names are rejected
            auto config = configStore->snapshot();
            if (name != "whatsapp_zapi" && name != "whatsapp_meta" && config->channels.find(name) == config->channels.end())
            {
                sendError(resp, "Channel not found", 404);
                return;
            }
        }

        // clang-format off
        configStore->update([&](ionclaw::config::Config &config) {
            // operator[] upserts, so a whatsapp channel that passed the pre-check is created on first configuration
            auto &ch = config.channels[name];

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

            // whatsapp string fields; a masked or empty secret keeps the stored value so it is not wiped
            static const std::vector<std::pair<std::string, bool>> whatsAppFields = {{"instance_id", false}, {"instance_token", true}, {"client_token", true}, {"access_token", true}, {"phone_number_id", false}, {"verify_token", true}, {"app_secret", true}, {"graph_version", false}};

            for (const auto &[key, secret] : whatsAppFields)
            {
                if (configData.contains(key) && configData[key].is_string())
                {
                    auto value = configData[key].get<std::string>();

                    if (secret && (value.empty() || value == "****"))
                    {
                        continue;
                    }

                    ch.raw[key] = value;
                }
            }

            ionclaw::config::ConfigLoader::save(config, config.projectPath + "/config.yml");
        });
        // clang-format on

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
        {
            auto config = configStore->snapshot();
            auto it = config->channels.find(name);

            if (it == config->channels.end())
            {
                sendError(resp, "Channel not found", 404);
                return;
            }

            if (it->second.running)
            {
                sendError(resp, "Channel already running", 409);
                return;
            }
        }

        channelManager->startChannel(name);

        // clang-format off
        configStore->update([&](ionclaw::config::Config &config) {
            auto it = config.channels.find(name);
            if (it != config.channels.end())
            {
                it->second.running = true;
            }
        });
        // clang-format on

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
        {
            auto config = configStore->snapshot();
            auto it = config->channels.find(name);

            if (it == config->channels.end())
            {
                sendError(resp, "Channel not found", 404);
                return;
            }

            if (!it->second.running)
            {
                sendError(resp, "Channel not running", 409);
                return;
            }
        }

        channelManager->stopChannel(name);

        // clang-format off
        configStore->update([&](ionclaw::config::Config &config) {
            auto it = config.channels.find(name);
            if (it != config.channels.end())
            {
                it->second.running = false;
            }
        });
        // clang-format on

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
