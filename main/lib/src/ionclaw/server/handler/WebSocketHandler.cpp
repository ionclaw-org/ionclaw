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
    std::string connectionId;

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

        // upgrade to websocket
        Poco::Net::WebSocket ws(req, resp);
        ws.setReceiveTimeout(Poco::Timespan(0, 0));

        connectionId = ionclaw::util::UniqueId::uuid();
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

                if (received <= 0 || (flags & Poco::Net::WebSocket::FRAME_OP_BITMASK) == Poco::Net::WebSocket::FRAME_OP_CLOSE)
                {
                    spdlog::info("[WebSocket] Connection {} closed (received={}, opcode={})",
                                 connectionId, received, flags & Poco::Net::WebSocket::FRAME_OP_BITMASK);
                    break;
                }

                std::string message(buffer, static_cast<size_t>(received));

                try
                {
                    auto json = nlohmann::json::parse(message);
                    auto type = json.value("type", "");

                    if (type == "ping")
                    {
                        nlohmann::json pong = {{"type", "pong"}};
                        auto pongStr = pong.dump();
                        std::lock_guard<std::mutex> lock(conn->sendMutex);
                        conn->socket.sendFrame(pongStr.data(), static_cast<int>(pongStr.size()),
                                               Poco::Net::WebSocket::FRAME_TEXT);
                    }
                }
                catch (const nlohmann::json::exception &e)
                {
                    spdlog::warn("[WebSocket] Invalid message JSON ({}): {}", connectionId, e.what());
                }
            }
            catch (const Poco::TimeoutException &)
            {
                continue;
            }
            catch (const std::exception &e)
            {
                spdlog::warn("[WebSocket] Receive error ({}): {}", connectionId, e.what());
                break;
            }
        }

        wsManager->removeConnection(connectionId);
        connectionId.clear();
    }
    catch (const std::exception &e)
    {
        spdlog::error("[WebSocket] Handler error: {}", e.what());

        if (!connectionId.empty())
        {
            wsManager->removeConnection(connectionId);
        }
    }
}

} // namespace handler
} // namespace server
} // namespace ionclaw
