#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

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
    ~WebSocketConnection();

    void start();
    void stop();

    // hand a frame to the connection's own sender thread so a slow client never blocks the caller
    void enqueue(const std::string &payload);

private:
    void senderLoop();

    std::deque<std::string> outbound;
    std::mutex outboundMutex;
    std::condition_variable outboundCv;
    std::thread senderThread;
    std::atomic<bool> running{false};
};

class WebSocketManager
{
public:
    WebSocketManager();

    void addConnection(std::shared_ptr<WebSocketConnection> conn);
    void removeConnection(const std::string &connectionId);
    void broadcast(const std::string &eventType, const nlohmann::json &data);

private:
    std::map<std::string, std::shared_ptr<WebSocketConnection>> connections;
    mutable std::mutex mutex;
};

} // namespace server
} // namespace ionclaw
