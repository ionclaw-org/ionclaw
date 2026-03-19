#pragma once

#include <memory>
#include <string>

#include "Poco/Net/HTTPRequestHandlerFactory.h"

#include "ionclaw/mcp/McpDispatcher.hpp"
#include "ionclaw/server/Auth.hpp"
#include "ionclaw/server/Routes.hpp"
#include "ionclaw/server/WebSocketManager.hpp"

namespace ionclaw
{
namespace server
{
namespace handler
{

class RequestHandlerFactory final : public Poco::Net::HTTPRequestHandlerFactory
{
public:
    RequestHandlerFactory(
        std::shared_ptr<Routes> routes,
        std::shared_ptr<Auth> auth,
        std::shared_ptr<WebSocketManager> wsManager,
        std::shared_ptr<ionclaw::mcp::McpDispatcher> mcpDispatcher,
        const std::string &webDir,
        const std::string &publicDir);

    Poco::Net::HTTPRequestHandler *createRequestHandler(const Poco::Net::HTTPServerRequest &req) override;

private:
    std::shared_ptr<Routes> routes;
    std::shared_ptr<Auth> auth;
    std::shared_ptr<WebSocketManager> wsManager;
    std::shared_ptr<ionclaw::mcp::McpDispatcher> mcpDispatcher;
    std::string webDir;
    std::string publicDir;
};

} // namespace handler
} // namespace server
} // namespace ionclaw
