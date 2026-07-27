#include "ionclaw/server/WebSocketManager.hpp"

#include <algorithm>
#include <limits>
#include <vector>

#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace server
{

// beyond this backlog a client is too slow to keep up, so the oldest frames are dropped to bound memory
static constexpr size_t MAX_OUTBOUND_BACKLOG = 4096;

WebSocketConnection::WebSocketConnection(Poco::Net::WebSocket ws, const std::string &id)
    : socket(std::move(ws))
    , connectionId(id)
{
}

WebSocketConnection::~WebSocketConnection()
{
    stop();
}

void WebSocketConnection::start()
{
    running.store(true);

    // clang-format off
    senderThread = std::thread([this] { senderLoop(); });
    // clang-format on
}

void WebSocketConnection::stop()
{
    {
        std::lock_guard<std::mutex> lock(outboundMutex);
        running.store(false);
    }

    outboundCv.notify_all();

    if (senderThread.joinable())
    {
        senderThread.join();
    }
}

void WebSocketConnection::enqueue(const std::string &payload)
{
    std::lock_guard<std::mutex> lock(outboundMutex);

    if (!running.load())
    {
        return;
    }

    if (outbound.size() >= MAX_OUTBOUND_BACKLOG)
    {
        outbound.pop_front();
        spdlog::warn("[WebSocketManager] Backlog full for {}, dropping oldest frame", connectionId);
    }

    outbound.push_back(payload);
    outboundCv.notify_one();
}

void WebSocketConnection::senderLoop()
{
    while (true)
    {
        std::string payload;

        {
            std::unique_lock<std::mutex> lock(outboundMutex);

            // clang-format off
            outboundCv.wait(lock, [this] { return !running.load() || !outbound.empty(); });
            // clang-format on

            if (!running.load() && outbound.empty())
            {
                return;
            }

            payload = std::move(outbound.front());
            outbound.pop_front();
        }

        try
        {
            std::lock_guard<std::mutex> sendLock(sendMutex);

            // clamp to int max because poco sendFrame takes an int size and would otherwise overflow
            auto frameSize = std::min(payload.size(), static_cast<size_t>(std::numeric_limits<int>::max()));
            socket.sendFrame(payload.data(), static_cast<int>(frameSize), Poco::Net::WebSocket::FRAME_TEXT);
        }
        catch (const std::exception &e)
        {
            // the client is gone, so stop draining and let the receive loop remove the connection
            spdlog::warn("[WebSocketManager] Failed to send to WebSocket {}: {}", connectionId, e.what());
            running.store(false);
            return;
        }
    }
}

WebSocketManager::WebSocketManager()
{
}

void WebSocketManager::addConnection(std::shared_ptr<WebSocketConnection> conn)
{
    conn->start();

    std::lock_guard<std::mutex> lock(mutex);
    connections[conn->connectionId] = conn;
    spdlog::info("[WebSocketManager] WebSocket connected: {}", conn->connectionId);
}

void WebSocketManager::removeConnection(const std::string &connectionId)
{
    std::lock_guard<std::mutex> lock(mutex);
    connections.erase(connectionId);
    spdlog::info("[WebSocketManager] WebSocket disconnected: {}", connectionId);
}

void WebSocketManager::broadcast(const std::string &eventType, const nlohmann::json &data)
{
    // serialize message payload
    nlohmann::json message = {
        {"type", eventType},
        {"data", data}};

    auto payload = message.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);

    // snapshot connections under lock, then enqueue outside it so no send blocks the caller
    std::vector<std::shared_ptr<WebSocketConnection>> snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex);
        snapshot.reserve(connections.size());

        for (auto &[id, conn] : connections)
        {
            snapshot.push_back(conn);
        }
    }

    for (auto &conn : snapshot)
    {
        conn->enqueue(payload);
    }
}

} // namespace server
} // namespace ionclaw
