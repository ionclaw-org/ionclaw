#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "ionclaw/bus/MessageBus.hpp"

namespace ionclaw
{
namespace heartbeat
{

class HeartbeatService
{
public:
    HeartbeatService(
        std::shared_ptr<ionclaw::bus::MessageBus> bus,
        const std::string &workspacePath,
        int interval,
        bool enabled);

    void start();
    void stop();
    void restart(int newInterval, bool newEnabled);

private:
    static const char *HEARTBEAT_PROMPT;

    void runLoop();
    void tick();
    std::string readHeartbeatFile() const;
    bool isHeartbeatEmpty(const std::string &content) const;

    std::shared_ptr<ionclaw::bus::MessageBus> bus;
    std::string heartbeatFilePath;
    std::atomic<int> interval;
    std::atomic<bool> enabled;

    std::atomic<bool> running{false};
    std::thread loopThread;
};

} // namespace heartbeat
} // namespace ionclaw
