#include "ionclaw/server/Routes.hpp"

namespace ionclaw
{
namespace server
{

void Routes::handleAgentsList(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp)
{
    nlohmann::json result = nlohmann::json::array();

    for (const auto &[name, agentCfg] : config->agents)
    {
        result.push_back({
            {"name", name},
            {"description", agentCfg.description},
            {"model", agentCfg.model},
        });
    }

    sendJson(resp, result);
}

void Routes::handleToolsList(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp)
{
    auto tools = orchestrator->getFlatToolDefinitions();
    sendJson(resp, tools);
}

void Routes::handleProvidersList(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp)
{
    nlohmann::json result = nlohmann::json::array();

    for (const auto &[name, provCfg] : config->providers)
    {
        // check if provider has a configured api key
        bool hasKey = false;
        auto credIt = config->credentials.find(provCfg.credential);

        if (credIt != config->credentials.end() && !credIt->second.key.empty())
        {
            hasKey = true;
        }

        // collect request headers
        nlohmann::json headers = nlohmann::json::object();

        for (const auto &[k, v] : provCfg.requestHeaders)
        {
            headers[k] = v;
        }

        result.push_back({
            {"name", name},
            {"configured", hasKey},
            {"credential", provCfg.credential},
            {"has_key", hasKey},
            {"base_url", provCfg.baseUrl},
            {"request_headers", headers},
            {"request_options", nlohmann::json::object()},
            {"model_params", provCfg.modelParams.is_null() ? nlohmann::json::object() : provCfg.modelParams},
        });
    }

    sendJson(resp, result);
}

void Routes::handleProviderUpdate(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &name)
{
    try
    {
        auto body = nlohmann::json::parse(readBody(req));
        auto provIt = config->providers.find(name);

        if (provIt == config->providers.end())
        {
            sendError(resp, "Provider not found", 404);
            return;
        }

        if (body.contains("base_url"))
        {
            provIt->second.baseUrl = body["base_url"].get<std::string>();
        }

        if (body.contains("timeout"))
        {
            provIt->second.timeout = body["timeout"].get<int>();
        }

        if (body.contains("credential"))
        {
            provIt->second.credential = body["credential"].get<std::string>();
        }

        sendJson(resp, {{"status", "ok"}});
    }
    catch (const std::exception &e)
    {
        sendError(resp, e.what());
    }
}

} // namespace server
} // namespace ionclaw
