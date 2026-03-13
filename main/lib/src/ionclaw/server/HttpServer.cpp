#include "ionclaw/server/HttpServer.hpp"

#include "Poco/Net/HTTPServerParams.h"
#include "Poco/Net/ServerSocket.h"
#include "Poco/Net/SocketAddress.h"

#include "spdlog/spdlog.h"

#include "ionclaw/server/handler/RequestHandlerFactory.hpp"

namespace ionclaw
{
namespace server
{

HttpServer::HttpServer(
    std::shared_ptr<Routes> routes,
    std::shared_ptr<Auth> auth,
    std::shared_ptr<WebSocketManager> wsManager,
    std::shared_ptr<ionclaw::mcp::McpDispatcher> mcpDispatcher,
    const ionclaw::config::ServerConfig &serverConfig,
    const std::string &webDir,
    const std::string &publicDir)
    : routes(routes)
    , auth(auth)
    , wsManager(wsManager)
    , mcpDispatcher(mcpDispatcher)
    , serverConfig(serverConfig)
    , webDir(webDir)
    , publicDir(publicDir)
{
}

void HttpServer::start()
{
    // bind server socket
    Poco::Net::SocketAddress address(serverConfig.host, serverConfig.port);
    Poco::Net::ServerSocket socket(address);

    // configure thread pool
    auto params = new Poco::Net::HTTPServerParams;
    params->setMaxQueued(64);
    params->setMaxThreads(16);
    params->setThreadIdleTime(Poco::Timespan(10, 0));

    // create and start server with request handler factory
    auto factory = new handler::RequestHandlerFactory(routes, auth, wsManager, mcpDispatcher, webDir, publicDir);

    server = std::make_unique<Poco::Net::HTTPServer>(factory, socket, params);
    server->start();

    spdlog::info("HTTP server started on {}:{}", serverConfig.host, serverConfig.port);
}

void HttpServer::stop()
{
    if (server)
    {
        server->stop();
        spdlog::info("HTTP server stopped");
    }
}

int HttpServer::port() const
{
    return server ? server->port() : 0;
}

} // namespace server
} // namespace ionclaw
