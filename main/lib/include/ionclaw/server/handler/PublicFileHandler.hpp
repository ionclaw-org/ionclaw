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
    // rootMode serves files whose url path is the file path itself (e.g. /report.pdf) and sends non-files to the app
    explicit PublicFileHandler(const std::string &publicDir, bool rootMode = false);

    void handleRequest(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp) override;

private:
    std::string publicDir;
    bool rootMode;
};

} // namespace handler
} // namespace server
} // namespace ionclaw
