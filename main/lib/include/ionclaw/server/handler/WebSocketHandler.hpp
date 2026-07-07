#pragma once

#include <memory>

#include "Poco/Net/HTTPRequestHandler.h"

#include "ionclaw/server/Auth.hpp"
#include "ionclaw/server/WebSocketManager.hpp"

namespace ionclaw
{
namespace server
{
namespace handler
{

class WebSocketHandler final : public Poco::Net::HTTPRequestHandler
{
public:
    WebSocketHandler(std::shared_ptr<Auth> auth, std::shared_ptr<WebSocketManager> wsManager);

    void handleRequest(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp) override;

private:
    std::shared_ptr<Auth> auth;
    std::shared_ptr<WebSocketManager> wsManager;
};

} // namespace handler
} // namespace server
} // namespace ionclaw
