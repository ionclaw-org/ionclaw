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
    Routes(std::shared_ptr<ionclaw::config::Config> config, std::shared_ptr<Auth> auth, std::shared_ptr<ionclaw::agent::Orchestrator> orchestrator, std::shared_ptr<ionclaw::channel::ChannelManager> channelManager, std::shared_ptr<ionclaw::heartbeat::HeartbeatService> heartbeatService, std::shared_ptr<ionclaw::cron::CronService> cronService, std::shared_ptr<ionclaw::session::SessionManager> sessionManager, std::shared_ptr<ionclaw::task::TaskManager> taskManager, std::shared_ptr<ionclaw::bus::MessageBus> bus, std::shared_ptr<ionclaw::bus::EventDispatcher> dispatcher, std::shared_ptr<WebSocketManager> wsManager, const std::string &webDir, const std::string &projectRoot, const std::string &publicDir, const std::string &workspaceDir);

    void handleAuthLogin(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);

    void handleChatSend(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleChatUpload(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleChatSessions(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleChatSession(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &sessionId);
    void handleChatSessionDelete(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &sessionId);
    void handleChatStop(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &sessionId);

    void handleTasksList(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleTaskGet(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &taskId);
    void handleTaskUpdate(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &taskId);
    void handleTaskStop(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &taskId);

    void handleAgentsList(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);

    void handleToolsList(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);

    void handleProvidersList(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);

    void handleConfigGet(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleConfigYaml(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleConfigUpdate(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleConfigSection(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &section);
    void handleConfigValidate(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleConfigRestart(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleConfigDeleteItem(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &section, const std::string &name);

    void handleEnvironmentVariablesList(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleEnvironmentVariablesUpdate(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);

    void handleHealth(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleVersion(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleSystemInfo(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);

    void handleSkillsList(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleSkillGet(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &name);
    void handleSkillUpdate(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &name);
    void handleSkillDelete(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &name);

    void handleMarketplaceTargets(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleMarketplaceCheck(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &source, const std::string &name);
    void handleMarketplaceInstall(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);

    void handleFilesList(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleFileRead(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &path);
    void handleFileWrite(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &path);
    void handleFileDelete(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &path);
    void handleFileMkdir(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &path);
    void handleFileCreate(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &path);
    void handleFileRename(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &path);
    void handleFileDownload(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &path);
    void handleFileUpload(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &path);

    void handleChannelsList(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleChannelGet(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &name);
    void handleChannelUpdate(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &name);
    void handleChannelStart(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &name);
    void handleChannelStop(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &name);

    void handleFormsList(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);

    void handleSchedulerList(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleSchedulerCreate(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleSchedulerUpdate(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &id);
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
    std::string projectRoot;
    std::string publicDir;
    std::string workspaceDir;

    std::mutex configMutex;

    static const std::set<std::string> PROTECTED_FILES;
    static const std::set<std::string> SYSTEM_FILES;
    static const std::set<std::string> SKIP_DIR_NAMES;

    static const char *MARKETPLACE_BASE;

    static std::string mediaDatePath();

    void sendJson(Poco::Net::HTTPServerResponse &resp, const nlohmann::json &body, int status = 200);
    void sendError(Poco::Net::HTTPServerResponse &resp, const std::string &message, int status = 400);
    std::string readBody(Poco::Net::HTTPServerRequest &req);
    std::string resolveSessionKey(const std::string &sessionId) const;

    nlohmann::json buildFileTree(const std::string &dirPath, const std::string &rootPath) const;
    nlohmann::json buildFileTreeFromProject(const std::string &dirPath, const std::string &rootPath) const;
    nlohmann::json buildFileTreeImpl(const std::string &dirPath, const std::string &rootPath, bool skipNonEssential) const;
    std::string detectFileType(const std::string &ext) const;

    static bool isProtectedFile(const std::string &path);
    static bool isHiddenPath(const std::string &path);
    static bool isSystemFile(const std::string &name);

    static bool isValidMarketplaceSegment(const std::string &s);

    std::string resolveFilePath(const std::string &relativePath) const;

    static nlohmann::json buildFormSchemas();

    static std::pair<bool, std::string> extractAgentParam(Poco::Net::HTTPServerRequest &req);
    static ionclaw::agent::SkillsLoader createSkillsLoader(const ionclaw::config::Config &cfg, const std::string &workspacePath);
    std::string resolveWorkspaceForSkill(const std::string &skillName) const;
};

} // namespace server
} // namespace ionclaw
