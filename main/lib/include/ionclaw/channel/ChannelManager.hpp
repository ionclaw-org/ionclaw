#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "ionclaw/config/Config.hpp"

namespace ionclaw
{
namespace bus
{
class EventDispatcher;
class MessageBus;
} // namespace bus
namespace mcp
{
class McpDispatcher;
}
namespace session
{
class SessionManager;
}
namespace task
{
class TaskManager;
}
namespace channel
{
class TelegramRunner;
}

namespace channel
{

class ChannelManager
{
public:
    ChannelManager(
        std::shared_ptr<ionclaw::config::Config> config,
        std::shared_ptr<ionclaw::bus::MessageBus> bus,
        std::shared_ptr<ionclaw::session::SessionManager> sessionManager,
        std::shared_ptr<ionclaw::task::TaskManager> taskManager,
        std::shared_ptr<ionclaw::bus::EventDispatcher> dispatcher,
        std::shared_ptr<ionclaw::mcp::McpDispatcher> mcpDispatcher);
    ~ChannelManager();

    void startChannel(const std::string &name);
    void stopChannel(const std::string &name);
    void stopAll();

private:
    void startTelegram();
    void stopTelegram();
    void startMcp();
    void stopMcp();

    std::shared_ptr<ionclaw::config::Config> config;
    std::shared_ptr<ionclaw::bus::MessageBus> bus;
    std::shared_ptr<ionclaw::session::SessionManager> sessionManager;
    std::shared_ptr<ionclaw::task::TaskManager> taskManager;
    std::shared_ptr<ionclaw::bus::EventDispatcher> dispatcher;
    std::shared_ptr<ionclaw::mcp::McpDispatcher> mcpDispatcher;

    std::mutex mutex;
    std::unique_ptr<TelegramRunner> telegramRunner;
};

} // namespace channel
} // namespace ionclaw
