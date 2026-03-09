#pragma once

#include <string>

#include "Poco/Net/HTTPRequestHandler.h"

namespace ionclaw
{
namespace server
{
namespace handler
{

class RedirectHandler : public Poco::Net::HTTPRequestHandler
{
public:
    explicit RedirectHandler(const std::string &location);

    void handleRequest(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp) override;

private:
    std::string location;
};

} // namespace handler
} // namespace server
} // namespace ionclaw
