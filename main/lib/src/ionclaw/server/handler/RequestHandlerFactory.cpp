#include "ionclaw/server/handler/RequestHandlerFactory.hpp"

#include "Poco/Net/HTTPServerRequest.h"
#include "Poco/URI.h"

#include "ionclaw/server/handler/ApiHandler.hpp"
#include "ionclaw/server/handler/McpHandler.hpp"
#include "ionclaw/server/handler/PublicFileHandler.hpp"
#include "ionclaw/server/handler/WebAppHandler.hpp"
#include "ionclaw/server/handler/WebSocketHandler.hpp"

namespace ionclaw
{
namespace server
{
namespace handler
{

RequestHandlerFactory::RequestHandlerFactory(std::shared_ptr<Routes> routes, std::shared_ptr<Auth> auth, std::shared_ptr<WebSocketManager> wsManager, std::shared_ptr<ionclaw::mcp::McpDispatcher> mcpDispatcher, const std::string &webDir, const std::string &publicDir)
    : routes(routes)
    , auth(auth)
    , wsManager(wsManager)
    , mcpDispatcher(mcpDispatcher)
    , webDir(webDir)
    , publicDir(publicDir)
{
}

// poco takes ownership of the raw handler pointer returned from createRequestHandler
Poco::Net::HTTPRequestHandler *RequestHandlerFactory::createRequestHandler(const Poco::Net::HTTPServerRequest &req)
{
    auto path = Poco::URI(req.getURI()).getPath();

    // websocket upgrade
    if (path == "/ws")
    {
        return new WebSocketHandler(auth, wsManager);
    }

    // mcp server endpoint
    if (path == "/mcp")
    {
        return new McpHandler(auth, mcpDispatcher);
    }

    // api routes
    if (path.substr(0, 4) == "/api")
    {
        return new ApiHandler(auth, routes);
    }

    // web application (spa)
    if (path == "/app" || path.substr(0, 5) == "/app/")
    {
        return new WebAppHandler(webDir);
    }

    // public file serving under the canonical /public/ prefix
    if (path == "/public" || path.substr(0, 8) == "/public/")
    {
        return new PublicFileHandler(publicDir);
    }

    // any other path is served as a public file if one exists at that name, otherwise it falls back to the app
    return new PublicFileHandler(publicDir, true);
}

} // namespace handler
} // namespace server
} // namespace ionclaw
