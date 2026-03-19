#pragma once

#include <string>

#include "Poco/Net/HTTPRequestHandler.h"

namespace ionclaw
{
namespace server
{
namespace handler
{

class PublicFileHandler final : public Poco::Net::HTTPRequestHandler
{
public:
    explicit PublicFileHandler(const std::string &publicDir);

    void handleRequest(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp) override;

private:
    std::string publicDir;
};

} // namespace handler
} // namespace server
} // namespace ionclaw
