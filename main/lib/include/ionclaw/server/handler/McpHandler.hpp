#pragma once

#include <memory>

#include "Poco/Net/HTTPRequestHandler.h"
#include "Poco/Net/HTTPServerRequest.h"
#include "Poco/Net/HTTPServerResponse.h"

#include "ionclaw/mcp/McpDispatcher.hpp"
#include "ionclaw/server/Auth.hpp"

namespace ionclaw
{
namespace server
{
namespace handler
{

class McpHandler final : public Poco::Net::HTTPRequestHandler
{
public:
    McpHandler(
        std::shared_ptr<Auth> auth,
        std::shared_ptr<ionclaw::mcp::McpDispatcher> mcpDispatcher);

    void handleRequest(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp) override;

private:
    void handlePost(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleGet(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);
    void handleDelete(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);

    bool checkAuth(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp);

    std::shared_ptr<Auth> auth;
    std::shared_ptr<ionclaw::mcp::McpDispatcher> mcpDispatcher;
};

} // namespace handler
} // namespace server
} // namespace ionclaw
