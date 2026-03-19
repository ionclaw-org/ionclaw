#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "ionclaw/agent/ContextBuilder.hpp"
#include "ionclaw/bus/EventDispatcher.hpp"
#include "ionclaw/bus/Events.hpp"
#include "ionclaw/bus/MessageBus.hpp"
#include "ionclaw/bus/SessionQueue.hpp"
#include "ionclaw/config/Config.hpp"
#include "ionclaw/provider/LlmProvider.hpp"
#include "ionclaw/session/SessionManager.hpp"
#include "ionclaw/task/TaskManager.hpp"
#include "ionclaw/tool/ToolRegistry.hpp"

namespace ionclaw
{
namespace agent
{

class HookRunner;
class SubagentRegistry;
struct ActiveTurnHandle;

struct AgentEvent
{
    std::string type;
    nlohmann::json data;
};

using AgentEventCallback = std::function<void(const AgentEvent &event)>;

struct UsageTracker
{
    // accumulated across all calls
    int64_t promptTokens = 0;
    int64_t completionTokens = 0;
    int64_t totalTokens = 0;
    int64_t cacheReadTokens = 0;
    int64_t cacheWriteTokens = 0;

    // last individual call (for accurate context size estimation)
    int64_t lastCallPromptTokens = 0;
    int64_t lastCallCompletionTokens = 0;
    int64_t lastCallTotalTokens = 0;
    int64_t lastCallCacheReadTokens = 0;
    int64_t lastCallCacheWriteTokens = 0;

    void record(const nlohmann::json &usage);
    nlohmann::json toJson() const;
};

struct StreamResult
{
    std::string content;
    std::string reasoningContent;
    std::vector<ionclaw::provider::ToolCall> toolCalls;
    std::string finishReason;
    nlohmann::json usage;
};

// per-turn mutable state, stack-allocated in processMessage to avoid data races
// when maxConcurrent > 1 (multiple threads sharing the same AgentLoop instance)
struct TurnState
{
    std::string lastSentContent;
    std::set<std::string> recentToolFingerprints;
    int memoryFlushCompactionCount = 0;
    int compactionCount = 0;
    ionclaw::bus::SessionQueue *sessionQueuePtr = nullptr;
    ActiveTurnHandle *activeTurnHandle = nullptr;
};

class AgentLoop
{
public:
    AgentLoop(
        std::shared_ptr<ionclaw::provider::LlmProvider> provider,
        std::shared_ptr<ionclaw::tool::ToolRegistry> toolRegistry,
        std::shared_ptr<ionclaw::session::SessionManager> sessionManager,
        std::shared_ptr<ionclaw::task::TaskManager> taskManager,
        std::shared_ptr<ionclaw::bus::EventDispatcher> dispatcher,
        const ionclaw::config::AgentConfig &agentConfig,
        const std::string &agentName);

    void processMessage(
        const ionclaw::bus::InboundMessage &message,
        const std::string &systemPrompt,
        AgentEventCallback callback);

    void stop();

    void setPublicPath(const std::string &path) { publicPath = path; }
    void setMessageSender(std::function<void(const std::string &, const std::string &, const std::string &)> sender) { messageSender = std::move(sender); }
    void setConfig(const ionclaw::config::Config *cfg) { configPtr = cfg; }
    void setBus(ionclaw::bus::MessageBus *b) { busPtr = b; }
    void setCronService(ionclaw::cron::CronService *cs) { cronServicePtr = cs; }
    void setSubagentRegistry(SubagentRegistry *sr) { subagentRegistryPtr = sr; }
    void setSessionQueue(ionclaw::bus::SessionQueue *sq) { defaultSessionQueuePtr.store(sq, std::memory_order_release); }
    void setActiveTurnHandle(ActiveTurnHandle *handle) { defaultActiveTurnHandle.store(handle, std::memory_order_release); }
    void setHookRunner(HookRunner *hr) { hookRunnerPtr = hr; }

private:
    static constexpr int TASK_RESULT_MAX_CHARS = 200;
    static constexpr size_t MAX_PROMPT_BYTES = 10 * 1024 * 1024;

    std::shared_ptr<ionclaw::provider::LlmProvider> provider;
    std::shared_ptr<ionclaw::tool::ToolRegistry> toolRegistry;
    std::shared_ptr<ionclaw::session::SessionManager> sessionManager;
    std::shared_ptr<ionclaw::task::TaskManager> taskManager;
    std::shared_ptr<ionclaw::bus::EventDispatcher> dispatcher;
    ionclaw::config::AgentConfig agentConfig;
    std::string agentName;
    std::string publicPath;
    std::function<void(const std::string &, const std::string &, const std::string &)> messageSender;
    const ionclaw::config::Config *configPtr = nullptr;
    ionclaw::bus::MessageBus *busPtr = nullptr;
    ionclaw::cron::CronService *cronServicePtr = nullptr;
    SubagentRegistry *subagentRegistryPtr = nullptr;
    std::atomic<ionclaw::bus::SessionQueue *> defaultSessionQueuePtr{nullptr};
    std::atomic<ActiveTurnHandle *> defaultActiveTurnHandle{nullptr};
    HookRunner *hookRunnerPtr = nullptr;
    std::atomic<bool> stopped{false};

    static std::string truncateText(const std::string &s, size_t maxLen);
    static std::string formatToolSummary(const std::string &name, const nlohmann::json &args);
    static std::string stripTrailingDirectives(const std::string &text);
    static size_t estimatePromptBytes(const std::vector<ionclaw::provider::Message> &messages);

    void sendCommandResponse(const ionclaw::bus::InboundMessage &message, const std::string &sessionKey, const std::string &taskId, const std::string &agentName, const std::string &responseText, AgentEventCallback &callback);

    nlohmann::json resolveMedia(const std::vector<std::string> &paths, const std::string &projectRoot);

    std::pair<std::string, std::vector<nlohmann::json>> runAgentLoop(
        std::vector<ionclaw::provider::Message> &messages,
        const std::string &taskId,
        const std::string &chatId,
        const std::string &sessionKey,
        const std::string &effectiveName,
        const ionclaw::tool::ToolContext &toolContext,
        AgentEventCallback &callback,
        TurnState &turnState);

    bool tryMemoryFlush(
        std::vector<ionclaw::provider::Message> &messages,
        const ionclaw::tool::ToolContext &toolContext,
        const nlohmann::json &modelParams,
        TurnState &turnState);
    void compactWithHooks(
        std::vector<ionclaw::provider::Message> &messages,
        const std::string &sessionKey,
        const std::string &taskId,
        const nlohmann::json &modelParams,
        TurnState &turnState,
        const ionclaw::tool::ToolContext *toolContext = nullptr);

    StreamResult consumeStream(
        const std::vector<ionclaw::provider::Message> &messages,
        const std::string &taskId,
        const std::string &chatId,
        const std::string &effectiveName,
        UsageTracker &usageTracker,
        AgentEventCallback &callback,
        const nlohmann::json &modelParams);
};

} // namespace agent
} // namespace ionclaw
