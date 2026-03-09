#pragma once

#include <functional>
#include <set>
#include <string>

#include "nlohmann/json.hpp"

#include "ionclaw/tool/Platform.hpp"

namespace ionclaw
{

// forward declarations to avoid circular includes
namespace config
{
struct Config;
}

namespace task
{
class TaskManager;
}

namespace bus
{
class MessageBus;
class EventDispatcher;
} // namespace bus

namespace cron
{
class CronService;
}

namespace agent
{
class SubagentRegistry;
class HookRunner;
} // namespace agent

namespace tool
{

struct ToolSchema
{
    std::string name;
    std::string description;
    nlohmann::json parameters;

    nlohmann::json toOpenAiFormat() const;
};

struct ToolContext
{
    std::string workspacePath;
    std::string publicPath;
    std::string sessionKey;
    std::string agentName;
    std::function<void(const std::string &channel, const std::string &chatId, const std::string &content)> messageSender;

    const ionclaw::config::Config *config = nullptr;
    ionclaw::task::TaskManager *taskManager = nullptr;
    ionclaw::bus::MessageBus *bus = nullptr;
    ionclaw::bus::EventDispatcher *dispatcher = nullptr;
    ionclaw::cron::CronService *cronService = nullptr;
    ionclaw::agent::SubagentRegistry *subagentRegistry = nullptr;
    ionclaw::agent::HookRunner *hookRunner = nullptr;
};

class Tool
{
public:
    virtual ~Tool() = default;
    virtual std::string execute(const nlohmann::json &params, const ToolContext &context) = 0;
    virtual ToolSchema schema() const = 0;

    // returns the set of OS names this tool supports (e.g. {"linux", "macos", "windows"}).
    // empty set means all platforms.
    virtual std::set<std::string> supportedPlatforms() const;
};

} // namespace tool
} // namespace ionclaw
