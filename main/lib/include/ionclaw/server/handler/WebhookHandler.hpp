#pragma once

#include <memory>

#include "Poco/Net/HTTPRequestHandler.h"
#include "Poco/Net/HTTPServerRequest.h"
#include "Poco/Net/HTTPServerResponse.h"

#include "ionclaw/server/Routes.hpp"

namespace ionclaw
{
namespace server
{
namespace handler
{

// routes inbound channel webhooks (unauthenticated; verified by provider signature/token)
class WebhookHandler final : public Poco::Net::HTTPRequestHandler
{
public:
    explicit WebhookHandler(std::shared_ptr<Routes> routes);

    void handleRequest(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp) override;

private:
    std::shared_ptr<Routes> routes;
};

} // namespace handler
} // namespace server
} // namespace ionclaw
