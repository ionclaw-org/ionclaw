#pragma once

#include <memory>
#include <string>

#include "Poco/Net/HTTPRequestHandler.h"
#include "Poco/Net/HTTPServerRequest.h"
#include "Poco/Net/HTTPServerResponse.h"

#include "ionclaw/server/Auth.hpp"
#include "ionclaw/server/Routes.hpp"

namespace ionclaw
{
namespace server
{
namespace handler
{

class ApiHandler final : public Poco::Net::HTTPRequestHandler
{
public:
    ApiHandler(
        std::shared_ptr<Auth> auth,
        std::shared_ptr<Routes> routes);

    void handleRequest(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp) override;

private:
    std::shared_ptr<Auth> auth;
    std::shared_ptr<Routes> routes;

    void routeRequest(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &path);
};

} // namespace handler
} // namespace server
} // namespace ionclaw
