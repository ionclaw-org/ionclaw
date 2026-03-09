#include "ionclaw/server/handler/RedirectHandler.hpp"

#include "Poco/Net/HTTPServerRequest.h"
#include "Poco/Net/HTTPServerResponse.h"

namespace ionclaw
{
namespace server
{
namespace handler
{

RedirectHandler::RedirectHandler(const std::string &location)
    : location(location)
{
}

void RedirectHandler::handleRequest(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp)
{
    resp.redirect(location, Poco::Net::HTTPResponse::HTTP_MOVED_PERMANENTLY);
}

} // namespace handler
} // namespace server
} // namespace ionclaw
