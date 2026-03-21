#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "ionclaw/bus/MessageBus.hpp"
#include "ionclaw/session/SessionManager.hpp"

namespace ionclaw
{
namespace heartbeat
{

class HeartbeatService
{
public:
    HeartbeatService(
        std::shared_ptr<ionclaw::bus::MessageBus> bus,
        std::shared_ptr<ionclaw::session::SessionManager> sessionManager,
        const std::string &workspacePath,
        int interval,
        bool enabled,
        const std::string &agent);

    void start();
    void stop();
    void restart(int newInterval, bool newEnabled, const std::string &newAgent);

private:
    static const char *HEARTBEAT_PROMPT;
    static const char *HEARTBEAT_SESSION_KEY;

    void runLoop();
    void tick();
    std::string readHeartbeatFile() const;
    static bool isHeartbeatEmpty(const std::string &content);

    std::shared_ptr<ionclaw::bus::MessageBus> bus;
    std::shared_ptr<ionclaw::session::SessionManager> sessionManager;
    std::string heartbeatFilePath;
    std::atomic<int> interval;
    std::atomic<bool> enabled;
    std::string agent;

    std::atomic<bool> running{false};
    std::thread loopThread;
    std::mutex agentMutex;
};

} // namespace heartbeat
} // namespace ionclaw
