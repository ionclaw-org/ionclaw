#include "ionclaw/server/WebSocketManager.hpp"

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
    std::vector<std::string> deadConnections;

    std::lock_guard<std::mutex> lock(mutex);

    // send to all connections, track failures
    for (auto &[id, conn] : connections)
    {
        try
        {
            std::lock_guard<std::mutex> sendLock(conn->sendMutex);
            conn->socket.sendFrame(payload.data(), static_cast<int>(payload.size()), Poco::Net::WebSocket::FRAME_TEXT);
        }
        catch (const std::exception &e)
        {
            spdlog::warn("Failed to send to WebSocket {}: {}", id, e.what());
            deadConnections.push_back(id);
        }
    }

    // remove dead connections
    for (const auto &id : deadConnections)
    {
        connections.erase(id);
        spdlog::info("Removed dead WebSocket connection: {}", id);
    }
}

void WebSocketManager::sendTo(const std::string &connectionId, const std::string &eventType, const nlohmann::json &data)
{
    nlohmann::json message = {
        {"type", eventType},
        {"data", data}};

    auto payload = message.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);

    std::lock_guard<std::mutex> lock(mutex);
    auto it = connections.find(connectionId);

    if (it == connections.end())
    {
        spdlog::warn("WebSocket connection not found: {}", connectionId);
        return;
    }

    try
    {
        std::lock_guard<std::mutex> sendLock(it->second->sendMutex);
        it->second->socket.sendFrame(payload.data(), static_cast<int>(payload.size()), Poco::Net::WebSocket::FRAME_TEXT);
    }
    catch (const std::exception &e)
    {
        spdlog::warn("Failed to send to WebSocket {}: {}", connectionId, e.what());
        connections.erase(it);
    }
}

size_t WebSocketManager::connectionCount() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return connections.size();
}

} // namespace server
} // namespace ionclaw
