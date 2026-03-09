#include "ionclaw/server/Routes.hpp"

namespace ionclaw
{
namespace server
{

void Routes::handleAuthLogin(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp)
{
    try
    {
        auto body = nlohmann::json::parse(readBody(req));
        auto username = body.value("username", "");
        auto password = body.value("password", "");

        auto token = auth->login(username, password);
        sendJson(resp, {{"token", token}, {"username", username}});
    }
    catch (const std::exception &e)
    {
        sendError(resp, e.what(), 401);
    }
}

} // namespace server
} // namespace ionclaw
