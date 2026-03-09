#pragma once

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "nlohmann/json.hpp"

#include "ionclaw/agent/AgentLoop.hpp"
#include "ionclaw/agent/AnnounceQueue.hpp"
#include "ionclaw/agent/Classifier.hpp"
#include "ionclaw/agent/ContextBuilder.hpp"
#include "ionclaw/agent/HookRunner.hpp"
#include "ionclaw/agent/MemoryStore.hpp"
#include "ionclaw/agent/SkillsLoader.hpp"
#include "ionclaw/agent/SubagentRegistry.hpp"
#include "ionclaw/bus/EventDispatcher.hpp"
#include "ionclaw/bus/MessageBus.hpp"
#include "ionclaw/bus/SessionQueue.hpp"
#include "ionclaw/config/Config.hpp"
#include "ionclaw/cron/CronService.hpp"
#include "ionclaw/provider/LlmProvider.hpp"
#include "ionclaw/session/SessionManager.hpp"
#include "ionclaw/task/TaskManager.hpp"
#include "ionclaw/tool/ToolRegistry.hpp"

namespace ionclaw
{
namespace agent
{

struct ActiveTurnHandle
{
    std::string agentName;
    std::string taskId;
    std::chrono::steady_clock::time_point startedAt;
    std::atomic<bool> aborted{false};
};

class Orchestrator
{
public:
    Orchestrator(
        std::shared_ptr<ionclaw::bus::MessageBus> bus,
        std::shared_ptr<ionclaw::bus::EventDispatcher> dispatcher,
        std::shared_ptr<ionclaw::session::SessionManager> sessionManager,
        std::shared_ptr<ionclaw::task::TaskManager> taskManager,
        std::shared_ptr<ionclaw::tool::ToolRegistry> toolRegistry,
        const ionclaw::config::Config &config);

    void start();
    void stop();
    void restart(const ionclaw::config::Config &newConfig);

    void setCronService(std::shared_ptr<ionclaw::cron::CronService> cs);

    std::vector<nlohmann::json> getToolDefinitions() const;
    std::vector<nlohmann::json> getFlatToolDefinitions() const;
    std::vector<std::string> getAgentNames() const;

    // active turn tracking (thread-safe, used by ChatRoutes for steer bypass)
    bool isSessionActive(const std::string &sessionKey) const;
    std::shared_ptr<ActiveTurnHandle> getActiveTurn(const std::string &sessionKey) const;

    // session queue accessor (used by ChatRoutes for steer bypass)
    ionclaw::bus::SessionQueue *getSessionQueue() const { return sessionQueue_.get(); }

private:
    std::shared_ptr<ionclaw::bus::MessageBus> bus;
    std::shared_ptr<ionclaw::bus::EventDispatcher> dispatcher;
    std::shared_ptr<ionclaw::session::SessionManager> sessionManager;
    std::shared_ptr<ionclaw::task::TaskManager> taskManager;
    std::shared_ptr<ionclaw::tool::ToolRegistry> toolRegistry;
    ionclaw::config::Config config;
    std::shared_ptr<ionclaw::cron::CronService> cronService;

    std::unique_ptr<Classifier> classifier;
    std::map<std::string, std::shared_ptr<AgentLoop>> agentLoops;
    std::map<std::string, std::shared_ptr<ionclaw::provider::LlmProvider>> providers;
    std::map<std::string, std::unique_ptr<ContextBuilder>> contextBuilders;
    std::map<std::string, std::shared_ptr<SkillsLoader>> skillsLoaders;

    std::shared_ptr<SubagentRegistry> subagentRegistry;
    std::shared_ptr<AnnounceQueue> announceQueue;
    std::shared_ptr<HookRunner> hookRunner;
    std::shared_ptr<ionclaw::bus::SessionQueue> sessionQueue_;

    std::atomic<bool> running{false};
    std::thread workerThread;

    // active turn tracking
    std::map<std::string, std::shared_ptr<ActiveTurnHandle>> activeTurns_;
    mutable std::mutex activeTurnsMutex_;

    void setActiveTurn(const std::string &sessionKey, std::shared_ptr<ActiveTurnHandle> handle);
    void clearActiveTurn(const std::string &sessionKey);
    int getAgentActiveTurnCount(const std::string &agentName) const;

    void run();
    void handleMessage(const ionclaw::bus::InboundMessage &message);
    void processMessageDirect(const ionclaw::bus::InboundMessage &message);
    void handleInterrupt(const std::string &sessionKey, const ionclaw::bus::InboundMessage &message);
    void drainSessionQueue(const std::string &sessionKey);
    void handleSubagentCompletion(const std::string &runId, const std::string &outcome, bool errored);
};

} // namespace agent
} // namespace ionclaw
