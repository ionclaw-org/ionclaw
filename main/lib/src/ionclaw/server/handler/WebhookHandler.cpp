#include "ionclaw/server/handler/WebhookHandler.hpp"

#include "Poco/URI.h"

#include "ionclaw/server/handler/HttpHelper.hpp"

namespace ionclaw
{
namespace server
{
namespace handler
{

WebhookHandler::WebhookHandler(std::shared_ptr<Routes> routes)
    : routes(routes)
{
}

void WebhookHandler::handleRequest(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp)
{
    HttpHelper::addCorsHeaders(resp);

    auto path = Poco::URI(req.getURI()).getPath();

    if (path == "/webhook/whatsapp-zapi")
    {
        routes->handleWhatsAppZApiWebhook(req, resp);
        return;
    }

    if (path == "/webhook/whatsapp-meta")
    {
        routes->handleWhatsAppMetaWebhook(req, resp);
        return;
    }

    resp.setStatus(Poco::Net::HTTPResponse::HTTP_NOT_FOUND);
    resp.setContentType("text/plain");
    auto &out = resp.send();
    out << "Not Found";
}

} // namespace handler
} // namespace server
} // namespace ionclaw
