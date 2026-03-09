#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "nlohmann/json.hpp"

#include "Poco/Net/WebSocket.h"

namespace ionclaw
{
namespace server
{

struct WebSocketConnection
{
    Poco::Net::WebSocket socket;
    std::string connectionId;
    std::mutex sendMutex;

    WebSocketConnection(Poco::Net::WebSocket ws, const std::string &id);
};

class WebSocketManager
{
public:
    WebSocketManager();

    void addConnection(std::shared_ptr<WebSocketConnection> conn);
    void removeConnection(const std::string &connectionId);
    void broadcast(const std::string &eventType, const nlohmann::json &data);
    void sendTo(const std::string &connectionId, const std::string &eventType, const nlohmann::json &data);
    size_t connectionCount() const;

private:
    std::map<std::string, std::shared_ptr<WebSocketConnection>> connections;
    mutable std::mutex mutex;
};

} // namespace server
} // namespace ionclaw
