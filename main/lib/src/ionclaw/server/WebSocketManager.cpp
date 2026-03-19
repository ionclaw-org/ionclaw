#include "ionclaw/server/WebSocketManager.hpp"

#include <algorithm>
#include <limits>
#include <vector>

#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace server
{

WebSocketConnection::WebSocketConnection(Poco::Net::WebSocket ws, const std::string &id)
    : socket(std::move(ws))
    , connectionId(id)
{
}

WebSocketManager::WebSocketManager()
{
}

void WebSocketManager::addConnection(std::shared_ptr<WebSocketConnection> conn)
{
    std::lock_guard<std::mutex> lock(mutex);
    connections[conn->connectionId] = conn;
    spdlog::info("WebSocket connected: {}", conn->connectionId);
}

void WebSocketManager::removeConnection(const std::string &connectionId)
{
    std::lock_guard<std::mutex> lock(mutex);
    connections.erase(connectionId);
    spdlog::info("WebSocket disconnected: {}", connectionId);
}

void WebSocketManager::broadcast(const std::string &eventType, const nlohmann::json &data)
{
    // serialize message payload
    nlohmann::json message = {
        {"type", eventType},
        {"data", data}};

    auto payload = message.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);

    // snapshot connections under lock to avoid holding mutex during I/O
    std::vector<std::pair<std::string, std::shared_ptr<WebSocketConnection>>> snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex);
        snapshot.reserve(connections.size());

        for (auto &[id, conn] : connections)
        {
            snapshot.emplace_back(id, conn);
        }
    }

    // send outside the manager lock
    std::vector<std::string> deadConnections;

    for (auto &[id, conn] : snapshot)
    {
        try
        {
            std::lock_guard<std::mutex> sendLock(conn->sendMutex);

            // poco sendFrame expects int for size; clamp to INT_MAX to prevent overflow
            auto frameSize = std::min(payload.size(), static_cast<size_t>(std::numeric_limits<int>::max()));
            conn->socket.sendFrame(payload.data(), static_cast<int>(frameSize), Poco::Net::WebSocket::FRAME_TEXT);
        }
        catch (const std::exception &e)
        {
            spdlog::warn("Failed to send to WebSocket {}: {}", id, e.what());
            deadConnections.push_back(id);
        }
    }

    // remove dead connections
    if (!deadConnections.empty())
    {
        std::lock_guard<std::mutex> lock(mutex);

        for (const auto &id : deadConnections)
        {
            connections.erase(id);
            spdlog::info("Removed dead WebSocket connection: {}", id);
        }
    }
}

void WebSocketManager::sendTo(const std::string &connectionId, const std::string &eventType, const nlohmann::json &data)
{
    nlohmann::json message = {
        {"type", eventType},
        {"data", data}};

    auto payload = message.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);

    // snapshot connection under lock to avoid holding mutex during I/O
    std::shared_ptr<WebSocketConnection> conn;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = connections.find(connectionId);

        if (it == connections.end())
        {
            spdlog::warn("WebSocket connection not found: {}", connectionId);
            return;
        }

        conn = it->second;
    }

    // send outside the manager lock
    try
    {
        std::lock_guard<std::mutex> sendLock(conn->sendMutex);

        // poco sendFrame expects int for size; clamp to INT_MAX to prevent overflow
        auto frameSize = std::min(payload.size(), static_cast<size_t>(std::numeric_limits<int>::max()));
        conn->socket.sendFrame(payload.data(), static_cast<int>(frameSize), Poco::Net::WebSocket::FRAME_TEXT);
    }
    catch (const std::exception &e)
    {
        spdlog::warn("Failed to send to WebSocket {}: {}", connectionId, e.what());
        std::lock_guard<std::mutex> lock(mutex);
        connections.erase(connectionId);
    }
}

size_t WebSocketManager::connectionCount() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return connections.size();
}

} // namespace server
} // namespace ionclaw
