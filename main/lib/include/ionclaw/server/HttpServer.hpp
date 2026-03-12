#pragma once

#include <memory>
#include <string>

#include "Poco/Net/HTTPServer.h"

#include "ionclaw/config/Config.hpp"
#include "ionclaw/mcp/McpDispatcher.hpp"
#include "ionclaw/server/Auth.hpp"
#include "ionclaw/server/Routes.hpp"
#include "ionclaw/server/WebSocketManager.hpp"

namespace ionclaw
{
namespace server
{

class HttpServer
{
public:
    HttpServer(
        std::shared_ptr<Routes> routes,
        std::shared_ptr<Auth> auth,
        std::shared_ptr<WebSocketManager> wsManager,
        std::shared_ptr<ionclaw::mcp::McpDispatcher> mcpDispatcher,
        const ionclaw::config::ServerConfig &serverConfig,
        const std::string &webDir,
        const std::string &publicDir);

    void start();
    void stop();
    int port() const;

private:
    std::shared_ptr<Routes> routes;
    std::shared_ptr<Auth> auth;
    std::shared_ptr<WebSocketManager> wsManager;
    std::shared_ptr<ionclaw::mcp::McpDispatcher> mcpDispatcher;
    ionclaw::config::ServerConfig serverConfig;
    std::string webDir;
    std::string publicDir;
    std::unique_ptr<Poco::Net::HTTPServer> server;
};

} // namespace server
} // namespace ionclaw
