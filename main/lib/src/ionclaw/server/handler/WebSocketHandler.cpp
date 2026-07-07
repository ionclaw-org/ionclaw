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
                    spdlog::info("[WebSocketHandler] Connection {} closed (received={}, opcode={})", connectionId, received, flags & Poco::Net::WebSocket::FRAME_OP_BITMASK);
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
