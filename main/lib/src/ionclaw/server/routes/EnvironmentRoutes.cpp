#include "ionclaw/server/Routes.hpp"

#include <map>
#include <string>

#include "ionclaw/util/EnvironmentHelper.hpp"

namespace ionclaw
{
namespace server
{

void Routes::handleEnvironmentVariablesList(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp)
{
    std::lock_guard<std::mutex> lock(configMutex);

    auto values = ionclaw::util::EnvironmentHelper::readDotEnv(config->projectPath);

    // expose only the keys and mask the values so secrets never leave the server
    nlohmann::json variables = nlohmann::json::array();

    for (const auto &[key, value] : values)
    {
        variables.push_back({{"key", key}, {"value", "****"}});
    }

    sendJson(resp, {{"variables", variables}});
}

void Routes::handleEnvironmentVariablesUpdate(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp)
{
    try
    {
        auto body = nlohmann::json::parse(readBody(req));
        auto incoming = body.value("variables", nlohmann::json::array());

        std::lock_guard<std::mutex> lock(configMutex);

        auto current = ionclaw::util::EnvironmentHelper::readDotEnv(config->projectPath);
        std::map<std::string, std::string> result;

        // the submitted list defines the kept keys, so any omitted key is deleted
        for (const auto &item : incoming)
        {
            std::string key = item.value("key", "");
            std::string value = item.value("value", "");

            if (key.empty())
            {
                continue;
            }

            // an empty or masked value keeps the current secret, a real value replaces it
            if (value.empty() || value == "****")
            {
                auto existing = current.find(key);

                if (existing != current.end())
                {
                    result[key] = existing->second;
                }

                continue;
            }

            result[key] = value;
        }

        ionclaw::util::EnvironmentHelper::writeDotEnv(config->projectPath, result);

        // drop removed keys from the running environment, then reapply the file as the source of truth
        for (const auto &entry : current)
        {
            if (result.find(entry.first) == result.end())
            {
                ionclaw::util::EnvironmentHelper::unset(entry.first);
            }
        }

        ionclaw::util::EnvironmentHelper::loadDotEnv(config->projectPath);

        sendJson(resp, {{"success", true}});
    }
    catch (const std::exception &e)
    {
        sendError(resp, e.what());
    }
}

} // namespace server
} // namespace ionclaw
