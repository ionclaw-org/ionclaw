#pragma once

#include <memory>
#include <mutex>
#include <string>

#include "ionclaw/agent/Orchestrator.hpp"
#include "ionclaw/bus/EventDispatcher.hpp"
#include "ionclaw/bus/MessageBus.hpp"
#include "ionclaw/channel/ChannelManager.hpp"
#include "ionclaw/config/Config.hpp"
#include "ionclaw/cron/CronService.hpp"
#include "ionclaw/heartbeat/HeartbeatService.hpp"
#include "ionclaw/mcp/McpDispatcher.hpp"
#include "ionclaw/server/Auth.hpp"
#include "ionclaw/server/HttpServer.hpp"
#include "ionclaw/server/Routes.hpp"
#include "ionclaw/server/WebSocketManager.hpp"
#include "ionclaw/session/SessionManager.hpp"
#include "ionclaw/task/TaskManager.hpp"
#include "ionclaw/tool/ToolRegistry.hpp"

namespace ionclaw
{
namespace server
{

struct ServerResult
{
    std::string host;
    int port = 0;
    bool success = false;
    std::string error;
};

class ServerInstance
{
public:
    static ServerResult start(const std::string &projectPath, const std::string &host, int port, const std::string &rootPath = "", const std::string &webPath = "");
    static ServerResult stop();

private:
    static std::shared_ptr<ionclaw::config::Config> config;
    static std::shared_ptr<ionclaw::bus::EventDispatcher> dispatcher;
    static std::shared_ptr<ionclaw::bus::MessageBus> bus;
    static std::shared_ptr<ionclaw::session::SessionManager> sessionManager;
    static std::shared_ptr<ionclaw::task::TaskManager> taskManager;
    static std::shared_ptr<ionclaw::tool::ToolRegistry> toolRegistry;
    static std::shared_ptr<WebSocketManager> wsManager;
    static std::shared_ptr<Auth> auth;
    static std::shared_ptr<ionclaw::agent::Orchestrator> orchestrator;
    static std::shared_ptr<ionclaw::mcp::McpDispatcher> mcpDispatcher;
    static std::shared_ptr<ionclaw::channel::ChannelManager> channelManager;
    static std::shared_ptr<ionclaw::heartbeat::HeartbeatService> heartbeatService;
    static std::shared_ptr<ionclaw::cron::CronService> cronService;
    static std::shared_ptr<Routes> routes;
    static std::shared_ptr<HttpServer> httpServer;
    static std::mutex mutex;

    // release all shared components (used by start error path and stop)
    static void resetComponents();
};

} // namespace server
} // namespace ionclaw
