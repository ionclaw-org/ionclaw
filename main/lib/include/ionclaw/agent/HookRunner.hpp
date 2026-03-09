#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace ionclaw
{
namespace agent
{

enum class HookPoint
{
    BeforeModelResolve,
    BeforeAgentStart,
    BeforePromptBuild,
    BeforeToolCall,
    AfterToolCall,
    MessageReceived,
    MessageSent,
    AgentTurnStart,
    AgentTurnEnd,
    BeforeCompaction,
    AfterCompaction,
    SubagentSpawning,
    SubagentSpawned,
    SubagentEnded,
};

struct HookContext
{
    std::string agentName;
    std::string sessionKey;
    std::string taskId;
    nlohmann::json data;

    // set by hooks to block execution (e.g. BeforeToolCall)
    bool blocked = false;
    std::string blockReason;
};

using HookCallback = std::function<void(HookContext &ctx)>;

class HookRunner
{
public:
    void registerHook(HookPoint point, HookCallback callback);
    void run(HookPoint point, HookContext &ctx) const;

private:
    std::map<HookPoint, std::vector<HookCallback>> hooks;
    mutable std::mutex mutex;
};

} // namespace agent
} // namespace ionclaw
