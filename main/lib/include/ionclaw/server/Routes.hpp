#pragma once

#include <memory>
#include <mutex>
#include <set>
#include <string>

#include "nlohmann/json.hpp"

#include "Poco/Net/HTTPServerRequest.h"
#include "Poco/Net/HTTPServerResponse.h"

#include "ionclaw/agent/Orchestrator.hpp"
#include "ionclaw/agent/SkillsLoader.hpp"
#include "ionclaw/bus/EventDispatcher.hpp"
#include "ionclaw/bus/MessageBus.hpp"
#include "ionclaw/channel/ChannelManager.hpp"
#include "ionclaw/config/Config.hpp"
#include "ionclaw/cron/CronService.hpp"
#include "ionclaw/heartbeat/HeartbeatService.hpp"
#include "ionclaw/server/Auth.hpp"
#include "ionclaw/server/WebSocketManager.hpp"
#include "ionclaw/session/SessionManager.hpp"
#include "ionclaw/task/TaskManager.hpp"

namespace ionclaw
{
namespace server
{

class Routes
{
public:
    Routes(
        std::shared_ptr<ionclaw::config::Config> config,
        std::shared_ptr<Auth> auth,
        std::shared_ptr<ionclaw::agent::Orchestrator> orchestrator,
        std::shared_ptr<ionclaw::channel::ChannelManager> channelManager,
        std::shared_ptr<ionclaw::heartbeat::HeartbeatService> heartbeatService,
        std::shared_ptr<ionclaw::cron::CronService> cronService,
        std::shared_ptr<ionclaw::session::SessionManager> sessionManager,
        std::shared_ptr<ionclaw::task::TaskManager> taskManager,
        std::shared_ptr<ionclaw::bus::MessageBus> bus,
        std::shared_ptr<ionclaw::bus::EventDispatcher> dispatcher,
        std::shared_ptr<WebSocketManager> wsManager,
        const std::string &webDir,
        const std::string &publicDir,
        const std::string &workspaceDir,
        const std::string &projectRoot);

    // auth
    void handleAuthLogin(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);

    // chat
    void handleChatSend(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleChatUpload(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleChatSessions(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleChatSession(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &sessionId);
    void handleChatSessionDelete(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &sessionId);

    // tasks
    void handleTasksList(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleTaskGet(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &taskId);
    void handleTaskUpdate(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &taskId);

    // agents
    void handleAgentsList(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);

    // tools
    void handleToolsList(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);

    // providers
    void handleProvidersList(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);

    // config
    void handleConfigGet(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleConfigYaml(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleConfigUpdate(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleConfigSection(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &section);
    void handleConfigValidate(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleConfigRestart(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);

    // system
    void handleHealth(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleVersion(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleSystemInfo(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);

    // skills
    void handleSkillsList(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleSkillGet(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &name);
    void handleSkillUpdate(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &name);
    void handleSkillDelete(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &name);

    // marketplace (skill install from catalog)
    void handleMarketplaceTargets(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleMarketplaceCheck(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &source, const std::string &name);
    void handleMarketplaceInstall(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);

    // files
    void handleFilesList(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleFileRead(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &path);
    void handleFileWrite(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &path);
    void handleFileDelete(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &path);
    void handleFileMkdir(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &path);
    void handleFileCreate(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &path);
    void handleFileRename(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &path);
    void handleFileDownload(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &path);
    void handleFileUpload(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &path);

    // channels
    void handleChannelsList(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleChannelGet(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &name);
    void handleChannelUpdate(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &name);
    void handleChannelStart(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &name);
    void handleChannelStop(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &name);

    // forms
    void handleFormsList(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);

    // scheduler
    void handleSchedulerList(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleSchedulerCreate(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleSchedulerDelete(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &id);

private:
    std::shared_ptr<ionclaw::config::Config> config;
    std::shared_ptr<Auth> auth;
    std::shared_ptr<ionclaw::agent::Orchestrator> orchestrator;
    std::shared_ptr<ionclaw::channel::ChannelManager> channelManager;
    std::shared_ptr<ionclaw::heartbeat::HeartbeatService> heartbeatService;
    std::shared_ptr<ionclaw::cron::CronService> cronService;
    std::shared_ptr<ionclaw::session::SessionManager> sessionManager;
    std::shared_ptr<ionclaw::task::TaskManager> taskManager;
    std::shared_ptr<ionclaw::bus::MessageBus> bus;
    std::shared_ptr<ionclaw::bus::EventDispatcher> dispatcher;
    std::shared_ptr<WebSocketManager> wsManager;
    std::string webDir;
    std::string publicDir;    // project_root + "/public"
    std::string workspaceDir; // default agent workspace; each agent can have its own folder under project_root
    std::string projectRoot;  // directory where server was started; file tree and paths are relative to this

    // protects concurrent config mutations and disk writes
    std::mutex configMutex_;

    // file access constants
    static const std::set<std::string> PROTECTED_FILES;
    static const std::set<std::string> SYSTEM_FILES;
    static const std::set<std::string> SKIP_DIR_NAMES;

    // marketplace
    static const char *MARKETPLACE_BASE;

    // chat helpers
    static std::string mediaDatePath();

    // response helpers
    void sendJson(Poco::Net::HTTPServerResponse &resp, const nlohmann::json &body, int status = 200);
    void sendError(Poco::Net::HTTPServerResponse &resp, const std::string &message, int status = 400);
    std::string readBody(Poco::Net::HTTPServerRequest &req);
    std::string resolveSessionKey(const std::string &sessionId) const;

    // file helpers
    nlohmann::json buildFileTree(const std::string &dirPath, const std::string &rootPath) const;
    nlohmann::json buildFileTreeFromProject(const std::string &dirPath, const std::string &rootPath) const;
    nlohmann::json buildFileTreeImpl(const std::string &dirPath, const std::string &rootPath, bool skipNonEssential) const;
    std::string detectFileType(const std::string &ext) const;

    // file access filtering
    static bool isProtectedFile(const std::string &path);
    static bool isHiddenPath(const std::string &path);
    static bool isSystemFile(const std::string &name);

    // resolves a relative file path to a canonical absolute path within projectRoot;
    // returns empty string if the path escapes the root or is invalid
    std::string resolveFilePath(const std::string &relativePath) const;

    // form schemas
    static nlohmann::json buildFormSchemas();

    // skill route helpers
    static std::pair<bool, std::string> extractAgentParam(Poco::Net::HTTPServerRequest &req);
    static ionclaw::agent::SkillsLoader createSkillsLoader(const ionclaw::config::Config &cfg, const std::string &workspacePath);
    std::string resolveWorkspaceForSkill(const std::string &skillName) const;
};

} // namespace server
} // namespace ionclaw
