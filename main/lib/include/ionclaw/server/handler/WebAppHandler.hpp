#pragma once

#include <string>

#include "Poco/Net/HTTPRequestHandler.h"

namespace ionclaw
{
namespace server
{
namespace handler
{

class WebAppHandler : public Poco::Net::HTTPRequestHandler
{
public:
    explicit WebAppHandler(const std::string &webDir);

    void handleRequest(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp) override;

private:
    std::string webDir;
};

} // namespace handler
} // namespace server
} // namespace ionclaw
