#include "ionclaw/server/handler/WebSocketHandler.hpp"

#include <vector>

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

WebSocketHandler::WebSocketHandler(std::shared_ptr<Auth> auth, std::shared_ptr<WebSocketManager> wsManager)
    : auth(auth)
    , wsManager(wsManager)
{
}

void WebSocketHandler::handleRequest(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp)
{
    std::string connectionId;

    try
    {
        // read the token from the websocket subprotocol so it never lands in a url or an access log
        std::string token;
        auto protoHeader = req.get("Sec-WebSocket-Protocol", "");

        // the client offers the marker protocol followed by the bearer token as the second entry
        std::vector<std::string> protocols;
        std::string current;

        for (char c : protoHeader)
        {
            if (c == ',')
            {
                protocols.push_back(current);
                current.clear();
                continue;
            }

            if (c != ' ' && c != '\t')
            {
                current += c;
            }
        }

        if (!current.empty())
        {
            protocols.push_back(current);
        }

        if (protocols.size() >= 2 && protocols[0] == "access_token")
        {
            token = protocols[1];
        }

        // verify authentication
        if (token.empty() || !auth->verifyToken(token))
        {
            resp.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_UNAUTHORIZED);
            resp.send();
            return;
        }

        // echo the marker protocol so the browser accepts the negotiated subprotocol
        resp.set("Sec-WebSocket-Protocol", "access_token");

        // upgrade to websocket
        Poco::Net::WebSocket ws(req, resp);

        // a finite timeout turns a silently dead peer into a periodic wakeup so the handler thread is never pinned forever
        ws.setReceiveTimeout(Poco::Timespan(60, 0));
        ws.setSendTimeout(Poco::Timespan(10, 0));

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

                int opcode = flags & Poco::Net::WebSocket::FRAME_OP_BITMASK;

                if (received <= 0 || opcode == Poco::Net::WebSocket::FRAME_OP_CLOSE)
                {
                    spdlog::info("[WebSocketHandler] Connection {} closed (received={}, opcode={})", connectionId, received, opcode);
                    break;
                }

                // answer a client ping and ignore its pong so control frames are never parsed as application json
                if (opcode == Poco::Net::WebSocket::FRAME_OP_PING)
                {
                    std::lock_guard<std::mutex> lock(conn->sendMutex);
                    conn->socket.sendFrame(buffer, received, Poco::Net::WebSocket::FRAME_FLAG_FIN | Poco::Net::WebSocket::FRAME_OP_PONG);
                    continue;
                }

                if (opcode == Poco::Net::WebSocket::FRAME_OP_PONG)
                {
                    continue;
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
                        conn->socket.sendFrame(pongStr.data(), static_cast<int>(pongStr.size()), Poco::Net::WebSocket::FRAME_TEXT);
                    }
                }
                catch (const nlohmann::json::exception &e)
                {
                    spdlog::warn("[WebSocketHandler] Invalid message JSON ({}): {}", connectionId, e.what());
                }
            }
            catch (const Poco::TimeoutException &)
            {
                // probe the peer so a dropped connection surfaces as a send failure instead of blocking a thread indefinitely
                try
                {
                    std::lock_guard<std::mutex> lock(conn->sendMutex);
                    conn->socket.sendFrame(nullptr, 0, Poco::Net::WebSocket::FRAME_FLAG_FIN | Poco::Net::WebSocket::FRAME_OP_PING);
                }
                catch (const std::exception &e)
                {
                    spdlog::info("[WebSocketHandler] Connection {} unreachable on keepalive ping: {}", connectionId, e.what());
                    break;
                }

                continue;
            }
            catch (const std::exception &e)
            {
                spdlog::warn("[WebSocketHandler] Receive error ({}): {}", connectionId, e.what());
                break;
            }
        }

        wsManager->removeConnection(connectionId);
        connectionId.clear();
    }
    catch (const std::exception &e)
    {
        spdlog::error("[WebSocketHandler] Handler error: {}", e.what());

        if (!connectionId.empty())
        {
            wsManager->removeConnection(connectionId);
        }
    }
}

} // namespace handler
} // namespace server
} // namespace ionclaw
