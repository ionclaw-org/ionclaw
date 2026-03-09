#include "ionclaw/server/Routes.hpp"

namespace ionclaw
{
namespace server
{

void Routes::handleCredentialsList(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp)
{
    nlohmann::json result = nlohmann::json::object();

    // return credentials with sensitive values masked
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

        result[name] = item;
    }

    sendJson(resp, result);
}

void Routes::handleCredentialCreate(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp)
{
    try
    {
        auto body = nlohmann::json::parse(readBody(req));
        auto name = body.value("name", "");
        auto data = body.value("data", nlohmann::json::object());

        if (name.empty())
        {
            sendError(resp, "Name is required");
            return;
        }

        if (config->credentials.count(name))
        {
            sendError(resp, "Credential already exists: " + name);
            return;
        }

        ionclaw::config::CredentialConfig cred;

        if (data.contains("type"))
        {
            cred.type = data["type"].get<std::string>();
        }

        if (data.contains("key"))
        {
            cred.key = data["key"].get<std::string>();
        }

        if (data.contains("username"))
        {
            cred.username = data["username"].get<std::string>();
        }

        if (data.contains("password"))
        {
            cred.password = data["password"].get<std::string>();
        }

        if (data.contains("token"))
        {
            cred.token = data["token"].get<std::string>();
        }

        cred.raw = data;
        config->credentials[name] = cred;

        sendJson(resp, {{"status", "ok"}});
    }
    catch (const std::exception &e)
    {
        sendError(resp, e.what());
    }
}

void Routes::handleCredentialUpdate(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &name)
{
    try
    {
        auto body = nlohmann::json::parse(readBody(req));
        auto &cred = config->credentials[name];

        if (body.contains("type"))
        {
            cred.type = body["type"].get<std::string>();
        }

        if (body.contains("key") && !body["key"].get<std::string>().empty())
        {
            cred.key = body["key"].get<std::string>();
        }

        if (body.contains("username"))
        {
            cred.username = body["username"].get<std::string>();
        }

        if (body.contains("password") && !body["password"].get<std::string>().empty())
        {
            cred.password = body["password"].get<std::string>();
        }

        if (body.contains("token") && !body["token"].get<std::string>().empty())
        {
            cred.token = body["token"].get<std::string>();
        }

        sendJson(resp, {{"status", "ok"}});
    }
    catch (const std::exception &e)
    {
        sendError(resp, e.what());
    }
}

void Routes::handleCredentialDelete(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp, const std::string &name)
{
    auto it = config->credentials.find(name);

    if (it == config->credentials.end())
    {
        sendError(resp, "Credential not found", 404);
        return;
    }

    config->credentials.erase(it);
    sendJson(resp, {{"status", "deleted"}});
}

} // namespace server
} // namespace ionclaw
