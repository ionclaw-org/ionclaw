#include "ionclaw/server/handler/ApiHandler.hpp"

#include "Poco/URI.h"

#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

#include "ionclaw/server/handler/HttpHelper.hpp"

namespace ionclaw
{
namespace server
{
namespace handler
{

ApiHandler::ApiHandler(
    std::shared_ptr<Auth> auth,
    std::shared_ptr<Routes> routes)
    : auth(auth)
    , routes(routes)
{
}

void ApiHandler::handleRequest(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp)
{
    HttpHelper::addCorsHeaders(resp);

    // handle cors preflight
    if (req.getMethod() == "OPTIONS")
    {
        resp.setStatus(Poco::Net::HTTPResponse::HTTP_NO_CONTENT);
        resp.send();
        return;
    }

    Poco::URI uri(req.getURI());
    auto path = uri.getPath();

    // check authorization for non-public paths
    if (!Auth::isPublicPath(path, req.getMethod()))
    {
        auto authHeader = req.get("Authorization", "");
        auto token = Auth::extractBearerToken(authHeader);

        if (token.empty() || !auth->verifyToken(token))
        {
            resp.setStatus(Poco::Net::HTTPResponse::HTTP_UNAUTHORIZED);
            resp.setContentType("application/json");
            auto &out = resp.send();
            out << R"({"error":"Unauthorized"})";
            return;
        }
    }

    // dispatch to route handler
    try
    {
        routeRequest(req, resp, path);
    }
    catch (const std::exception &e)
    {
        spdlog::error("API error on {}: {}", path, e.what());
        resp.setStatus(Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR);
        resp.setContentType("application/json");

        if (!resp.sent())
        {
            auto &out = resp.send();
            nlohmann::json err = {{"error", e.what()}};
            out << err.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }
    }
}

void ApiHandler::routeRequest(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &path)
{
    auto method = req.getMethod();

    // health (public)
    if (path == "/api/health" && method == "GET")
    {
        routes->handleHealth(req, resp);
        return;
    }

    // version (public)
    if (path == "/api/version" && method == "GET")
    {
        routes->handleVersion(req, resp);
        return;
    }

    // auth
    if (path == "/api/auth/login" && method == "POST")
    {
        routes->handleAuthLogin(req, resp);
        return;
    }

    // chat
    if (path == "/api/chat" && method == "POST")
    {
        routes->handleChatSend(req, resp);
        return;
    }

    if (path == "/api/chat/upload" && method == "POST")
    {
        routes->handleChatUpload(req, resp);
        return;
    }

    if (path == "/api/chat/sessions" && method == "GET")
    {
        routes->handleChatSessions(req, resp);
        return;
    }

    auto sessionId = HttpHelper::extractPathParam(path, "/api/chat/sessions/");

    if (!sessionId.empty())
    {
        if (method == "GET")
        {
            routes->handleChatSession(req, resp, sessionId);
            return;
        }

        if (method == "DELETE")
        {
            routes->handleChatSessionDelete(req, resp, sessionId);
            return;
        }
    }

    // tasks
    if (path == "/api/tasks" && method == "GET")
    {
        routes->handleTasksList(req, resp);
        return;
    }

    auto taskId = HttpHelper::extractPathParam(path, "/api/tasks/");

    if (!taskId.empty())
    {
        if (method == "GET")
        {
            routes->handleTaskGet(req, resp, taskId);
            return;
        }

        if (method == "PUT" || method == "PATCH")
        {
            routes->handleTaskUpdate(req, resp, taskId);
            return;
        }
    }

    // agents
    if (path == "/api/agents" && method == "GET")
    {
        routes->handleAgentsList(req, resp);
        return;
    }

    // tools
    if (path == "/api/tools" && method == "GET")
    {
        routes->handleToolsList(req, resp);
        return;
    }

    // providers
    if (path == "/api/providers" && method == "GET")
    {
        routes->handleProvidersList(req, resp);
        return;
    }

    // config (specific routes before generic)
    if (path == "/api/config/yaml" && method == "GET")
    {
        routes->handleConfigYaml(req, resp);
        return;
    }

    if (path == "/api/config/validate" && method == "POST")
    {
        routes->handleConfigValidate(req, resp);
        return;
    }

    if (path == "/api/config/restart" && method == "POST")
    {
        routes->handleConfigRestart(req, resp);
        return;
    }

    if (path == "/api/config" && method == "GET")
    {
        routes->handleConfigGet(req, resp);
        return;
    }

    if (path == "/api/config" && method == "PATCH")
    {
        routes->handleConfigUpdate(req, resp);
        return;
    }

    // config section update: PUT /api/config/{section}
    auto configSection = HttpHelper::extractPathParam(path, "/api/config/");

    if (!configSection.empty() && method == "PUT")
    {
        routes->handleConfigSection(req, resp, configSection);
        return;
    }

    // system
    if (path == "/api/system/info" && method == "GET")
    {
        routes->handleSystemInfo(req, resp);
        return;
    }

    // skills
    if (path == "/api/skills" && method == "GET")
    {
        routes->handleSkillsList(req, resp);
        return;
    }

    auto skillName = HttpHelper::extractPathParam(path, "/api/skills/");

    if (!skillName.empty())
    {
        if (method == "GET")
        {
            routes->handleSkillGet(req, resp, skillName);
            return;
        }

        if (method == "PUT" || method == "POST")
        {
            routes->handleSkillUpdate(req, resp, skillName);
            return;
        }

        if (method == "DELETE")
        {
            routes->handleSkillDelete(req, resp, skillName);
            return;
        }
    }

    // marketplace
    if (path == "/api/marketplace/targets" && method == "GET")
    {
        routes->handleMarketplaceTargets(req, resp);
        return;
    }

    if (path == "/api/marketplace/install" && method == "POST")
    {
        routes->handleMarketplaceInstall(req, resp);
        return;
    }

    // marketplace check: /api/marketplace/check/{source}/{name}
    auto checkSuffix = HttpHelper::extractPathParam(path, "/api/marketplace/check/");

    if (!checkSuffix.empty() && method == "GET")
    {
        auto slash = checkSuffix.find('/');
        std::string source = (slash != std::string::npos) ? checkSuffix.substr(0, slash) : checkSuffix;
        std::string name = (slash != std::string::npos) ? checkSuffix.substr(slash + 1) : "";
        if (!name.empty())
        {
            // decode URL-encoded parameters (e.g. names with special characters)
            std::string decodedSource, decodedName;
            Poco::URI::decode(source, decodedSource);
            Poco::URI::decode(name, decodedName);
            routes->handleMarketplaceCheck(req, resp, decodedSource, decodedName);
            return;
        }
    }

    // files (specific operations before catch-all)
    if (path.substr(0, 10) == "/api/files")
    {
        auto downloadPath = HttpHelper::extractPathParam(path, "/api/files/download/");

        if (!downloadPath.empty() && method == "GET")
        {
            routes->handleFileDownload(req, resp, downloadPath);
            return;
        }

        auto mkdirPath = HttpHelper::extractPathParam(path, "/api/files/mkdir/");

        if (!mkdirPath.empty() && method == "POST")
        {
            routes->handleFileMkdir(req, resp, mkdirPath);
            return;
        }

        auto createPath = HttpHelper::extractPathParam(path, "/api/files/create/");

        if (!createPath.empty() && method == "POST")
        {
            routes->handleFileCreate(req, resp, createPath);
            return;
        }

        auto renamePath = HttpHelper::extractPathParam(path, "/api/files/rename/");

        if (!renamePath.empty() && method == "POST")
        {
            routes->handleFileRename(req, resp, renamePath);
            return;
        }

        // file upload
        if ((path == "/api/files/upload" || path == "/api/files/upload/") && method == "POST")
        {
            routes->handleFileUpload(req, resp, "");
            return;
        }

        auto uploadPath = HttpHelper::extractPathParam(path, "/api/files/upload/");

        if (!uploadPath.empty() && method == "POST")
        {
            routes->handleFileUpload(req, resp, uploadPath);
            return;
        }

        // generic file operations
        auto filePath = HttpHelper::extractPathParam(path, "/api/files/");

        if (method == "GET")
        {
            if (filePath.empty())
            {
                routes->handleFilesList(req, resp);
            }
            else
            {
                routes->handleFileRead(req, resp, filePath);
            }

            return;
        }

        if (method == "PUT" && !filePath.empty())
        {
            routes->handleFileWrite(req, resp, filePath);
            return;
        }

        if (method == "DELETE" && !filePath.empty())
        {
            routes->handleFileDelete(req, resp, filePath);
            return;
        }
    }

    // channels
    if (path == "/api/channels" && method == "GET")
    {
        routes->handleChannelsList(req, resp);
        return;
    }

    auto channelName = HttpHelper::extractPathParam(path, "/api/channels/");

    if (!channelName.empty())
    {
        // check for start/stop sub-routes
        auto slashPos = channelName.find('/');

        if (slashPos != std::string::npos)
        {
            auto subAction = channelName.substr(slashPos + 1);
            auto realName = channelName.substr(0, slashPos);

            if (subAction == "start" && method == "POST")
            {
                routes->handleChannelStart(req, resp, realName);
                return;
            }

            if (subAction == "stop" && method == "POST")
            {
                routes->handleChannelStop(req, resp, realName);
                return;
            }
        }

        if (method == "GET")
        {
            routes->handleChannelGet(req, resp, channelName);
            return;
        }

        if (method == "PUT")
        {
            routes->handleChannelUpdate(req, resp, channelName);
            return;
        }
    }

    // forms
    if (path == "/api/forms" && method == "GET")
    {
        routes->handleFormsList(req, resp);
        return;
    }

    // scheduler
    if (path == "/api/scheduler/jobs" && method == "GET")
    {
        routes->handleSchedulerList(req, resp);
        return;
    }

    if (path == "/api/scheduler/jobs" && method == "POST")
    {
        routes->handleSchedulerCreate(req, resp);
        return;
    }

    auto schedulerId = HttpHelper::extractPathParam(path, "/api/scheduler/jobs/");

    if (!schedulerId.empty() && method == "DELETE")
    {
        routes->handleSchedulerDelete(req, resp, schedulerId);
        return;
    }

    // not found
    resp.setStatus(Poco::Net::HTTPResponse::HTTP_NOT_FOUND);
    resp.setContentType("application/json");
    auto &out = resp.send();
    out << R"({"error":"Not found"})";
}

} // namespace handler
} // namespace server
} // namespace ionclaw
