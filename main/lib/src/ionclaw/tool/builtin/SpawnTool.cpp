#include "ionclaw/tool/builtin/SpawnTool.hpp"

#include "ionclaw/agent/HookRunner.hpp"
#include "ionclaw/agent/SubagentRegistry.hpp"
#include "ionclaw/bus/Events.hpp"
#include "ionclaw/bus/MessageBus.hpp"
#include "ionclaw/config/Config.hpp"
#include "ionclaw/task/TaskManager.hpp"
#include "ionclaw/util/StringHelper.hpp"
#include "ionclaw/util/UniqueId.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

std::string SpawnTool::execute(const nlohmann::json &params, const ToolContext &context)
{
    auto task = params.at("task").get<std::string>();

    std::string label;

    if (params.contains("label") && params["label"].is_string())
    {
        label = params["label"].get<std::string>();
    }
    else
    {
        label = ionclaw::util::StringHelper::utf8SafeTruncate(task, 50);

        if (task.size() > 50)
        {
            label += "...";
        }
    }

    std::string model;

    if (params.contains("model") && params["model"].is_string())
    {
        model = params["model"].get<std::string>();
    }

    std::string thinking;

    if (params.contains("thinking") && params["thinking"].is_string())
    {
        thinking = params["thinking"].get<std::string>();
    }

    // per-spawn timeout override (0 = use default)
    int runTimeoutSeconds = 0;

    if (params.contains("runTimeoutSeconds") && params["runTimeoutSeconds"].is_number())
    {
        runTimeoutSeconds = std::max(0, params["runTimeoutSeconds"].get<int>());
    }

    if (!context.taskManager)
    {
        return "Error: task manager not available";
    }

    if (!context.bus)
    {
        return "Error: message bus not available";
    }

    // validate depth and child count via subagent registry
    int parentDepth = 0;

    if (context.subagentRegistry)
    {
        parentDepth = context.subagentRegistry->getDepth(context.sessionKey);

        int maxDepth = ionclaw::agent::SubagentRegistry::MAX_DEPTH;
        int maxChildren = ionclaw::agent::SubagentRegistry::MAX_CHILDREN;

        // use agent-specific limits if config is available
        if (context.config)
        {
            auto agentIt = context.config->agents.find(context.agentName);

            if (agentIt != context.config->agents.end())
            {
                maxDepth = agentIt->second.subagentLimits.maxDepth;
                maxChildren = agentIt->second.subagentLimits.maxChildren;
            }
        }

        if (parentDepth >= maxDepth)
        {
            return "Error: maximum subagent depth (" + std::to_string(maxDepth) + ") reached";
        }

        auto activeChildren = context.subagentRegistry->getActiveChildCount(context.sessionKey);

        if (activeChildren >= maxChildren)
        {
            return "Error: maximum concurrent children (" + std::to_string(maxChildren) + ") reached, wait for existing subagents to complete";
        }
    }

    // fire SubagentSpawning hook (can block)
    if (context.hookRunner)
    {
        ionclaw::agent::HookContext hookCtx;
        hookCtx.agentName = context.agentName;
        hookCtx.sessionKey = context.sessionKey;
        hookCtx.data = {
            {"task", task},
            {"model", model},
            {"thinking", thinking},
            {"depth", parentDepth + 1},
        };
        context.hookRunner->run(ionclaw::agent::HookPoint::SubagentSpawning, hookCtx);

        if (hookCtx.blocked)
        {
            return "Error: Spawn blocked" +
                   (hookCtx.blockReason.empty() ? "" : ": " + hookCtx.blockReason);
        }
    }

    // parse channel and chatId from session key
    std::string channel = "web";
    std::string chatId = context.sessionKey;

    auto colonPos = context.sessionKey.find(':');

    if (colonPos != std::string::npos)
    {
        channel = context.sessionKey.substr(0, colonPos);
        chatId = context.sessionKey.substr(colonPos + 1);
    }

    // create new session key for child
    auto childChatId = ionclaw::util::UniqueId::uuid();
    auto childSessionKey = channel + ":" + childChatId;

    // create task
    auto createdTask = context.taskManager->createTask(label, task, channel, chatId);
    auto taskId = createdTask.id;

    // register subagent run
    std::string runId;

    if (context.subagentRegistry)
    {
        // resolve timeout: per-spawn override > config default > registry default
        int effectiveTimeout = runTimeoutSeconds;

        if (effectiveTimeout <= 0 && context.config)
        {
            auto agentIt = context.config->agents.find(context.agentName);

            if (agentIt != context.config->agents.end() && agentIt->second.subagentLimits.defaultTimeoutSeconds > 0)
            {
                effectiveTimeout = agentIt->second.subagentLimits.defaultTimeoutSeconds;
            }
        }

        if (effectiveTimeout <= 0)
        {
            effectiveTimeout = ionclaw::agent::SubagentRegistry::DEFAULT_TIMEOUT_SECONDS;
        }

        auto record = context.subagentRegistry->spawn(
            context.sessionKey, task, childSessionKey, model, thinking, parentDepth, effectiveTimeout);
        runId = record.runId;
    }

    // publish inbound message with subagent metadata
    ionclaw::bus::InboundMessage msg;
    msg.channel = channel;
    msg.chatId = childChatId;
    msg.content = task;
    msg.metadata = {
        {"task_id", taskId},
        {"parent_session_key", context.sessionKey},
        {"spawned_by", context.agentName},
        {"depth", parentDepth + 1},
    };

    if (!runId.empty())
    {
        msg.metadata["subagent_run_id"] = runId;
    }

    if (!model.empty())
    {
        msg.metadata["model"] = model;
    }

    if (!thinking.empty())
    {
        msg.metadata["thinking"] = thinking;
    }

    context.bus->publishInbound(msg);

    // fire SubagentSpawned hook
    if (context.hookRunner)
    {
        ionclaw::agent::HookContext hookCtx;
        hookCtx.agentName = context.agentName;
        hookCtx.sessionKey = context.sessionKey;
        hookCtx.data = {
            {"task", task},
            {"run_id", runId},
            {"child_session_key", childSessionKey},
            {"depth", parentDepth + 1},
        };
        context.hookRunner->run(ionclaw::agent::HookPoint::SubagentSpawned, hookCtx);
    }

    return "Spawned subagent: " + (runId.empty() ? taskId : runId) + " (" + label + ")";
}

ToolSchema SpawnTool::schema() const
{
    return {
        "spawn",
        "Spawn a background subagent task. The task will be processed asynchronously by a child agent.",
        {{"type", "object"},
         {"properties",
          {{"task", {{"type", "string"}, {"description", "The task description for the subagent"}}},
           {"label", {{"type", "string"}, {"description", "Short label for the task (default: truncated task description)"}}},
           {"model", {{"type", "string"}, {"description", "Override model for the subagent (optional)"}}},
           {"thinking", {{"type", "string"}, {"description", "Thinking level for the subagent: low, medium, high (optional)"}}},
           {"runTimeoutSeconds", {{"type", "number"}, {"description", "Timeout in seconds for this subagent run (0 or omit = use default)"}}}}},
         {"required", nlohmann::json::array({"task"})}}};
}

std::set<std::string> SpawnTool::supportedPlatforms() const
{
    return {"linux", "macos", "windows"};
}

} // namespace builtin
} // namespace tool
} // namespace ionclaw
