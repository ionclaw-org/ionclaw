#include "ionclaw/server/handler/WebSocketHandler.hpp"

#include "Poco/Net/HTTPServerRequest.h"
#include "Poco/Net/HTTPServerResponse.h"
#include "Poco/Net/WebSocket.h"
#include "Poco/URI.h"

#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

#include "ionclaw/util/UniqueId.hpp"

namespace ionclaw
{
namespace server
{
namespace handler
{

WebSocketHandler::WebSocketHandler(
    std::shared_ptr<Auth> auth,
    std::shared_ptr<WebSocketManager> wsManager,
    std::shared_ptr<Routes> routes)
    : auth(auth)
    , wsManager(wsManager)
    , routes(routes)
{
}

void WebSocketHandler::handleRequest(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp)
{
    try
    {
        // extract token from query parameter
        Poco::URI uri(req.getURI());
        std::string token;

        for (const auto &param : uri.getQueryParameters())
        {
            if (param.first == "token")
            {
                token = param.second;
            }
        }

        // verify authentication
        if (token.empty() || !auth->verifyToken(token))
        {
            resp.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_UNAUTHORIZED);
            resp.send();
            return;
        }

        // upgrade to websocket and register connection
        Poco::Net::WebSocket ws(req, resp);

        auto connectionId = ionclaw::util::UniqueId::uuid();
        auto conn = std::make_shared<WebSocketConnection>(std::move(ws), connectionId);
        wsManager->addConnection(conn);

        // message receive loop
        char buffer[65536];
        int flags = 0;

        while (true)
        {
            try
            {
                int received = conn->socket.receiveFrame(buffer, sizeof(buffer), flags);

                // detect close or disconnect
                if (received <= 0 || (flags & Poco::Net::WebSocket::FRAME_OP_BITMASK) == Poco::Net::WebSocket::FRAME_OP_CLOSE)
                {
                    break;
                }

                std::string message(buffer, received);

                // parse and handle json messages
                try
                {
                    auto json = nlohmann::json::parse(message);
                    auto type = json.value("type", "");

                    if (type == "ping")
                    {
                        nlohmann::json pong = {{"type", "pong"}};
                        auto pongStr = pong.dump();
                        std::lock_guard<std::mutex> lock(conn->sendMutex);
                        conn->socket.sendFrame(pongStr.data(), static_cast<int>(pongStr.size()), Poco::Net::WebSocket::FRAME_TEXT);
                    }
                    else if (type == "chat.send")
                    {
                        auto data = json.value("data", nlohmann::json::object());
                        spdlog::debug("WebSocket chat.send from {}: {}", connectionId, data.dump());
                    }
                }
                catch (const nlohmann::json::exception &e)
                {
                    spdlog::warn("Invalid WebSocket message JSON: {}", e.what());
                }
            }
            catch (const Poco::TimeoutException &)
            {
                continue;
            }
            catch (const std::exception &e)
            {
                spdlog::debug("WebSocket receive error: {}", e.what());
                break;
            }
        }

        wsManager->removeConnection(connectionId);
    }
    catch (const std::exception &e)
    {
        spdlog::error("WebSocket handler error: {}", e.what());
    }
}

} // namespace handler
} // namespace server
} // namespace ionclaw
