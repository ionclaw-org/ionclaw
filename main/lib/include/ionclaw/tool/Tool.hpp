#pragma once

#include <functional>
#include <set>
#include <string>

#include "nlohmann/json.hpp"

#include "ionclaw/tool/Platform.hpp"

namespace ionclaw
{

namespace config
{
struct Config;
}

namespace task
{
class TaskManager;
}

namespace session
{
class SessionManager;
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

struct ToolResult
{
    std::string text;
    nlohmann::json media;

    ToolResult() = default;
    ToolResult(const std::string &s)
        : text(s)
    {
    } // NOLINT(implicit)
    ToolResult(std::string &&s)
        : text(std::move(s))
    {
    } // NOLINT(implicit)
    ToolResult(const char *s)
        : text(s)
    {
    } // NOLINT(implicit)

    bool hasMedia() const { return media.is_array() && !media.empty(); }
};

struct ToolContext
{
    std::string projectPath;
    std::string workspacePath;
    std::string publicPath;
    std::string sessionKey;
    std::string agentName;
    std::function<void(const std::string &channel, const std::string &chatId, const std::string &content)> messageSender;

    // returns true once the user stops the turn, so long-running tools can bail and clean up cooperatively
    std::function<bool()> isCancelled;

    const ionclaw::config::Config *config = nullptr;
    ionclaw::task::TaskManager *taskManager = nullptr;
    ionclaw::session::SessionManager *sessionManager = nullptr;
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
    virtual ToolResult execute(const nlohmann::json &params, const ToolContext &context) = 0;
    virtual ToolSchema schema() const = 0;
    virtual std::set<std::string> supportedPlatforms() const;
};

} // namespace tool
} // namespace ionclaw
