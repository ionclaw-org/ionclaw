#include "ionclaw/heartbeat/HeartbeatService.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "ionclaw/bus/Events.hpp"
#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace heartbeat
{

const char *HeartbeatService::HEARTBEAT_PROMPT =
    "Read HEARTBEAT.md in your workspace (if it exists).\n"
    "Follow any instructions or tasks listed there.\n"
    "If nothing needs attention, reply with just: HEARTBEAT_OK";

HeartbeatService::HeartbeatService(
    std::shared_ptr<ionclaw::bus::MessageBus> bus,
    const std::string &workspacePath,
    int interval,
    bool enabled)
    : bus(std::move(bus))
    , heartbeatFilePath(workspacePath + "/HEARTBEAT.md")
    , interval(interval)
    , enabled(enabled)
{
}

void HeartbeatService::start()
{
    if (!enabled)
    {
        spdlog::info("[Heartbeat] disabled");
        return;
    }

    if (running.exchange(true))
    {
        return;
    }

    loopThread = std::thread(&HeartbeatService::runLoop, this);
    spdlog::info("[Heartbeat] started (every {}s)", interval);
}

void HeartbeatService::stop()
{
    if (!running.exchange(false))
    {
        return;
    }

    if (loopThread.joinable())
    {
        loopThread.join();
    }

    spdlog::info("[Heartbeat] stopped");
}

void HeartbeatService::restart(int newInterval, bool newEnabled)
{
    stop();
    interval = newInterval;
    enabled = newEnabled;
    start();
}

void HeartbeatService::runLoop()
{
    while (running.load())
    {
        // sleep in small increments to allow fast shutdown
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(interval);

        while (running.load() && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        if (!running.load())
        {
            break;
        }

        // execute heartbeat tick
        try
        {
            tick();
        }
        catch (const std::exception &e)
        {
            spdlog::error("[Heartbeat] error: {}", e.what());
        }
    }
}

void HeartbeatService::tick()
{
    auto content = readHeartbeatFile();

    if (isHeartbeatEmpty(content))
    {
        spdlog::debug("[Heartbeat] no tasks");
        return;
    }

    // publish inbound message to trigger agent processing
    spdlog::info("[Heartbeat] sending tasks to agent");

    ionclaw::bus::InboundMessage msg;
    msg.channel = "heartbeat";
    msg.senderId = "heartbeat";
    msg.chatId = "heartbeat";
    msg.content = HEARTBEAT_PROMPT;

    bus->publishInbound(msg);
}

std::string HeartbeatService::readHeartbeatFile() const
{
    if (!std::filesystem::exists(heartbeatFilePath))
    {
        return "";
    }

    std::ifstream file(heartbeatFilePath);

    if (!file.is_open())
    {
        return "";
    }

    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

bool HeartbeatService::isHeartbeatEmpty(const std::string &content) const
{
    if (content.empty())
    {
        return true;
    }

    // check if file has any actionable content (skip headers and comments)
    std::istringstream stream(content);
    std::string line;

    while (std::getline(stream, line))
    {
        // trim leading whitespace
        auto start = line.find_first_not_of(" \t");

        if (start == std::string::npos)
        {
            continue;
        }

        auto trimmed = line.substr(start);

        // skip markdown headers and html comments
        if (trimmed.empty() || trimmed[0] == '#' || trimmed.substr(0, 4) == "<!--")
        {
            continue;
        }

        // found actionable content
        return false;
    }

    return true;
}

} // namespace heartbeat
} // namespace ionclaw
