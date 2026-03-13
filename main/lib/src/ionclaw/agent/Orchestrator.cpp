#include "ionclaw/agent/Orchestrator.hpp"

#include "ionclaw/provider/ProviderFactory.hpp"
#include "ionclaw/util/TimeHelper.hpp"
#include "ionclaw/util/UniqueId.hpp"
#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace agent
{

Orchestrator::Orchestrator(
    std::shared_ptr<ionclaw::bus::MessageBus> bus,
    std::shared_ptr<ionclaw::bus::EventDispatcher> dispatcher,
    std::shared_ptr<ionclaw::session::SessionManager> sessionManager,
    std::shared_ptr<ionclaw::task::TaskManager> taskManager,
    std::shared_ptr<ionclaw::tool::ToolRegistry> toolRegistry,
    const ionclaw::config::Config &config)
    : bus(std::move(bus))
    , dispatcher(std::move(dispatcher))
    , sessionManager(std::move(sessionManager))
    , taskManager(std::move(taskManager))
    , toolRegistry(std::move(toolRegistry))
    , config(config)
{
}

void Orchestrator::setCronService(std::shared_ptr<ionclaw::cron::CronService> cs)
{
    cronService = std::move(cs);

    for (auto &[name, loop] : agentLoops)
    {
        loop->setCronService(cronService.get());
    }
}

// active turn tracking

bool Orchestrator::isSessionActive(const std::string &sessionKey) const
{
    std::lock_guard<std::mutex> lock(activeTurnsMutex_);
    return activeTurns_.find(sessionKey) != activeTurns_.end();
}

std::shared_ptr<ActiveTurnHandle> Orchestrator::getActiveTurn(const std::string &sessionKey) const
{
    std::lock_guard<std::mutex> lock(activeTurnsMutex_);
    auto it = activeTurns_.find(sessionKey);
    return (it != activeTurns_.end()) ? it->second : nullptr;
}

void Orchestrator::setActiveTurn(const std::string &sessionKey, std::shared_ptr<ActiveTurnHandle> handle)
{
    std::lock_guard<std::mutex> lock(activeTurnsMutex_);
    activeTurns_[sessionKey] = std::move(handle);
}

void Orchestrator::clearActiveTurn(const std::string &sessionKey)
{
    std::lock_guard<std::mutex> lock(activeTurnsMutex_);
    activeTurns_.erase(sessionKey);
}

int Orchestrator::getAgentActiveTurnCount(const std::string &agentName) const
{
    std::lock_guard<std::mutex> lock(activeTurnsMutex_);
    int count = 0;

    for (const auto &[key, handle] : activeTurns_)
    {
        if (handle && handle->agentName == agentName)
        {
            count++;
        }
    }

    return count;
}

// initialize providers, agent loops, classifier and start worker thread
void Orchestrator::start()
{
    if (running.load())
    {
        spdlog::warn("[Orchestrator] Already running");
        return;
    }

    spdlog::info("[Orchestrator] Starting...");

    // create hook runner
    hookRunner = std::make_shared<HookRunner>();

    // create subagent registry and announce queue
    subagentRegistry = std::make_shared<SubagentRegistry>(config.projectPath);
    subagentRegistry->load();
    subagentRegistry->recoverStaleRuns();
    announceQueue = std::make_shared<AnnounceQueue>();

    // create session queue for queue mode routing
    sessionQueue_ = std::make_shared<ionclaw::bus::SessionQueue>();

    // mark sessions that were active during previous crash as aborted
    sessionManager->setAbortCutoffAll();

    // create LLM providers and agent loops for each agent
    std::shared_ptr<ionclaw::provider::LlmProvider> firstProvider;

    for (const auto &[name, agentConfig] : config.agents)
    {
        try
        {
            // fire hook before model resolution
            HookContext hookCtx;
            hookCtx.agentName = name;
            hookCtx.data = {{"model", agentConfig.model}};
            hookRunner->run(HookPoint::BeforeModelResolve, hookCtx);

            // create provider: use failover if profiles are configured
            std::shared_ptr<ionclaw::provider::LlmProvider> provider;

            if (!agentConfig.profiles.empty())
            {
                provider = ionclaw::provider::ProviderFactory::createFailoverFromProfiles(
                    agentConfig.profiles, agentConfig.model, config);
            }
            else
            {
                provider = ionclaw::provider::ProviderFactory::createFromModel(agentConfig.model, config);
            }

            providers[name] = provider;

            if (!firstProvider)
            {
                firstProvider = provider;
            }

            // merge provider-level model_params as defaults under agent-level model_params
            auto resolvedAgentConfig = agentConfig;
            auto providerConfig = config.resolveProvider(agentConfig.model);

            if (providerConfig.modelParams.is_object() && !providerConfig.modelParams.empty())
            {
                // provider params are the base; agent params override
                auto merged = providerConfig.modelParams;
                if (resolvedAgentConfig.modelParams.is_object())
                {
                    merged.merge_patch(resolvedAgentConfig.modelParams);
                }
                resolvedAgentConfig.modelParams = merged;
            }

            // create memory store and skills loader for this agent
            auto workspacePath = resolvedAgentConfig.workspace;
            auto memory = std::make_shared<MemoryStore>(workspacePath);
            auto skills = std::make_shared<SkillsLoader>(config.projectPath, workspacePath);
            skillsLoaders[name] = skills;

            // create context builder with full context
            auto builder = std::make_unique<ContextBuilder>(config, workspacePath, memory, skills);
            contextBuilders[name] = std::move(builder);

            // create agent loop with dispatcher
            auto loop = std::make_shared<AgentLoop>(
                provider,
                toolRegistry,
                sessionManager,
                taskManager,
                dispatcher,
                resolvedAgentConfig,
                name);

            // set public path for tools
            if (!config.publicDir.empty())
            {
                loop->setPublicPath(config.publicDir);
            }

            // set message sender for tools that need to send messages
            auto busRef = this->bus;
            loop->setMessageSender([busRef](const std::string &channel, const std::string &chatId, const std::string &content)
                                   {
                ionclaw::bus::OutboundMessage msg;
                msg.channel = channel;
                msg.chatId = chatId;
                msg.content = content;
                busRef->publishOutbound(msg); });

            // wire config, bus, cron service, and subagent registry pointers for tool context
            loop->setConfig(&config);
            loop->setBus(this->bus.get());
            loop->setSubagentRegistry(subagentRegistry.get());
            loop->setHookRunner(hookRunner.get());

            if (cronService)
            {
                loop->setCronService(cronService.get());
            }

            agentLoops[name] = loop;

            // fire BeforeAgentStart hook
            {
                HookContext startCtx;
                startCtx.agentName = name;
                startCtx.data = {{"model", agentConfig.model}};
                hookRunner->run(HookPoint::BeforeAgentStart, startCtx);
            }

            spdlog::info("[Orchestrator] Initialized agent: {} (model: {})", name, agentConfig.model);
        }
        catch (const std::exception &e)
        {
            spdlog::error("[Orchestrator] Failed to initialize agent '{}': {}", name, e.what());
        }
    }

    if (agentLoops.empty())
    {
        spdlog::error("[Orchestrator] No agents initialized");
        return;
    }

    // create classifier
    if (firstProvider)
    {
        if (!config.classifier.model.empty())
        {
            try
            {
                auto classifierProvider = ionclaw::provider::ProviderFactory::createFromModel(config.classifier.model, config);
                classifier = std::make_unique<Classifier>(classifierProvider, config);
            }
            catch (const std::exception &e)
            {
                spdlog::warn("[Orchestrator] Failed to create classifier provider, using first agent provider: {}", e.what());
                classifier = std::make_unique<Classifier>(firstProvider, config);
            }
        }
        else
        {
            classifier = std::make_unique<Classifier>(firstProvider, config);
        }
    }

    // start worker thread
    running.store(true);
    workerThread = std::thread(&Orchestrator::run, this);

    spdlog::info("[Orchestrator] Started with {} agent(s)", agentLoops.size());
}

// message bus consumer loop running on worker thread
void Orchestrator::run()
{
    spdlog::info("[Orchestrator] Worker thread started");

    while (running.load())
    {
        try
        {
            // periodically clean up expired announce entries
            if (announceQueue)
            {
                announceQueue->processExpired();
            }

            ionclaw::bus::InboundMessage message;

            if (bus->consumeInbound(message, 500))
            {
                try
                {
                    handleMessage(message);
                }
                catch (const std::exception &e)
                {
                    spdlog::error("[Orchestrator] Error handling message: {}", e.what());

                    try
                    {
                        auto sessionKey = message.sessionKey();
                        dispatcher->broadcast("chat:message", {
                                                                  {"chat_id", sessionKey},
                                                                  {"content", std::string("I encountered an error: ") + e.what()},
                                                                  {"error", e.what()},
                                                              });

                        dispatcher->broadcast("chat:stream_end", {
                                                                     {"chat_id", sessionKey},
                                                                 });
                    }
                    catch (...)
                    {
                        spdlog::error("[Orchestrator] Failed to broadcast error notification");
                    }
                }
            }
        }
        catch (const std::exception &e)
        {
            spdlog::error("[Orchestrator] Worker loop error: {}", e.what());
        }
        catch (...)
        {
            spdlog::error("[Orchestrator] Worker loop unknown error");
        }
    }

    spdlog::info("[Orchestrator] Worker thread stopped");
}

// queue-aware message routing
void Orchestrator::handleMessage(const ionclaw::bus::InboundMessage &message)
{
    auto sessionKey = message.sessionKey();
    auto channel = message.channel.empty() ? "web" : message.channel;

    // fire MessageReceived hook
    if (hookRunner)
    {
        HookContext hookCtx;
        hookCtx.sessionKey = sessionKey;
        hookCtx.data = {{"content", message.content}, {"channel", channel}};

        if (message.metadata.contains("task_id"))
        {
            hookCtx.taskId = message.metadata["task_id"].get<std::string>();
        }

        hookRunner->run(HookPoint::MessageReceived, hookCtx);
    }

    // resolve effective queue mode
    auto settings = ionclaw::bus::resolveQueueSettings(config, channel, message.queueMode);

    // check if session has an active turn
    bool isActive = isSessionActive(sessionKey);

    if (!isActive)
    {
        // no active turn: process immediately regardless of mode
        processMessageDirect(message);
        return;
    }

    // session has an active turn: route based on queue mode
    switch (settings.mode)
    {
    case ionclaw::bus::QueueMode::Steer:
        sessionQueue_->enqueue(sessionKey, message, ionclaw::bus::QueueMode::Steer, settings);
        spdlog::info("[Orchestrator] Steer message queued for active session {}", sessionKey);
        break;

    case ionclaw::bus::QueueMode::SteerBacklog:
        // try steer; also enqueue as followup backup
        sessionQueue_->enqueue(sessionKey, message, ionclaw::bus::QueueMode::Steer, settings);
        sessionQueue_->enqueue(sessionKey, message, ionclaw::bus::QueueMode::Followup, settings);
        spdlog::info("[Orchestrator] SteerBacklog: steer + followup queued for {}", sessionKey);
        break;

    case ionclaw::bus::QueueMode::Followup:
        sessionQueue_->enqueue(sessionKey, message, ionclaw::bus::QueueMode::Followup, settings);
        spdlog::info("[Orchestrator] Followup queued for active session {}", sessionKey);
        break;

    case ionclaw::bus::QueueMode::Collect:
        sessionQueue_->enqueue(sessionKey, message, ionclaw::bus::QueueMode::Collect, settings);
        spdlog::info("[Orchestrator] Collect message queued for active session {}", sessionKey);
        break;

    case ionclaw::bus::QueueMode::Interrupt:
        handleInterrupt(sessionKey, message);
        break;
    }
}

// process a message directly (no queueing) — the main agent turn
void Orchestrator::processMessageDirect(const ionclaw::bus::InboundMessage &message)
{
    auto sessionKey = message.sessionKey();
    auto channel = message.channel.empty() ? "web" : message.channel;

    // check if this is a subagent message and update registry
    std::string subagentRunId;

    if (message.metadata.contains("subagent_run_id") && message.metadata["subagent_run_id"].is_string())
    {
        subagentRunId = message.metadata["subagent_run_id"].get<std::string>();

        if (subagentRegistry)
        {
            subagentRegistry->updateStatus(subagentRunId, SubagentStatus::Active);
        }
    }

    // drain pending announces for this session
    std::string announceText;

    if (announceQueue)
    {
        auto announces = announceQueue->drain(sessionKey);

        for (const auto &entry : announces)
        {
            if (!announceText.empty())
            {
                announceText += "\n\n";
            }

            announceText += entry.message;
        }
    }

    // prepend announce text to message content
    auto effectiveContent = message.content;

    if (!announceText.empty())
    {
        effectiveContent = "[Subagent results]:\n" + announceText + "\n\n" + effectiveContent;
    }

    // get session history for classifier context
    auto history = sessionManager->getHistory(sessionKey, 20);

    // resolve target agent: check metadata override, then session affinity, then classify
    std::string targetAgent;

    // explicit agent override from caller (e.g. MCP tool `agent` parameter)
    if (message.metadata.contains("agent_override") && message.metadata["agent_override"].is_string())
    {
        auto requestedAgent = message.metadata["agent_override"].get<std::string>();
        if (!requestedAgent.empty() && agentLoops.find(requestedAgent) != agentLoops.end())
        {
            targetAgent = requestedAgent;
            spdlog::debug("[Orchestrator] Using metadata agent override: {} (session: {})", targetAgent, sessionKey);
        }
    }

    // session agent affinity: reuse last agent for multi-agent setups
    if (targetAgent.empty() && agentLoops.size() > 1)
    {
        auto session = sessionManager->getOrCreate(sessionKey);

        if (session.liveState.contains("agentAffinity") && session.liveState["agentAffinity"].is_string())
        {
            auto affinity = session.liveState["agentAffinity"].get<std::string>();

            if (agentLoops.find(affinity) != agentLoops.end())
            {
                targetAgent = affinity;
                spdlog::debug("[Orchestrator] Using session agent affinity: {} (session: {})", targetAgent, sessionKey);
            }
        }
    }

    // classify only when no affinity exists (new session or affinity agent removed)
    if (targetAgent.empty() && classifier && agentLoops.size() > 1)
    {
        targetAgent = classifier->classify(effectiveContent, sessionKey, history);
    }

    if (targetAgent.empty() || agentLoops.find(targetAgent) == agentLoops.end())
    {
        targetAgent = agentLoops.begin()->first;
        spdlog::debug("[Orchestrator] Falling back to default agent: {}", targetAgent);
    }

    // persist agent affinity for this session
    if (agentLoops.size() > 1)
    {
        sessionManager->updateLiveStateField(sessionKey, "agentAffinity", targetAgent);
    }

    spdlog::info("[Orchestrator] Routing message to agent '{}' (session: {})", targetAgent, sessionKey);

    // enforce per-agent concurrency limit
    auto agentIt2 = config.agents.find(targetAgent);
    int maxConcurrent = 1;

    if (agentIt2 != config.agents.end())
    {
        maxConcurrent = std::max(1, agentIt2->second.agentParams.maxConcurrent);
    }

    int activeTurns = getAgentActiveTurnCount(targetAgent);

    if (activeTurns >= maxConcurrent)
    {
        spdlog::warn("[Orchestrator] Agent '{}' at concurrency limit ({}/{}), queueing as followup",
                     targetAgent, activeTurns, maxConcurrent);
        sessionQueue_->enqueue(sessionKey, message, ionclaw::bus::QueueMode::Followup,
                               ionclaw::bus::resolveQueueSettings(config, channel));
        return;
    }

    // create active turn handle
    auto turnHandle = std::make_shared<ActiveTurnHandle>();
    turnHandle->agentName = targetAgent;
    turnHandle->startedAt = std::chrono::steady_clock::now();

    if (message.metadata.contains("task_id"))
    {
        turnHandle->taskId = message.metadata["task_id"].get<std::string>();
    }

    setActiveTurn(sessionKey, turnHandle);

    // get the agent loop
    auto &loop = agentLoops[targetAgent];

    // wire session queue and turn handle into agent loop for steer/abort
    loop->setSessionQueue(sessionQueue_.get());
    loop->setActiveTurnHandle(turnHandle.get());

    // fire AgentTurnStart and BeforePromptBuild hooks
    if (hookRunner)
    {
        HookContext hookCtx;
        hookCtx.agentName = targetAgent;
        hookCtx.sessionKey = sessionKey;
        hookRunner->run(HookPoint::AgentTurnStart, hookCtx);
        hookRunner->run(HookPoint::BeforePromptBuild, hookCtx);
    }

    // build system prompt with full context
    auto builderIt = contextBuilders.find(targetAgent);

    if (builderIt == contextBuilders.end() || !builderIt->second)
    {
        spdlog::error("[Orchestrator] No context builder for agent '{}', falling back to default", targetAgent);
        return;
    }

    auto &builder = builderIt->second;
    auto agentIt = config.agents.find(targetAgent);

    auto agentTools = (agentIt != config.agents.end()) ? agentIt->second.tools : std::vector<std::string>{};
    auto baseTools = agentTools.empty() ? toolRegistry->getToolNames() : agentTools;
    static const ionclaw::config::ToolPolicy emptyPolicy;
    auto &agentToolPolicy = (agentIt != config.agents.end()) ? agentIt->second.toolPolicy : emptyPolicy;
    auto effectiveTools = ionclaw::tool::ToolRegistry::applyToolPolicy(baseTools, agentToolPolicy);
    auto toolNames = toolRegistry->getToolNames(effectiveTools);

    std::string agentInstructions;

    if (agentIt != config.agents.end())
    {
        agentInstructions = agentIt->second.instructions;
    }

    auto promptMode = subagentRunId.empty() ? PromptMode::Full : PromptMode::Minimal;
    auto systemPrompt = builder->buildSystemPrompt(targetAgent, agentInstructions, channel, toolNames, promptMode);

    // append subagent context if this is a subagent session
    if (!subagentRunId.empty() && subagentRegistry)
    {
        int depth = subagentRegistry->getDepth(sessionKey);
        int maxDepth = SubagentRegistry::MAX_DEPTH;

        if (agentIt != config.agents.end())
        {
            maxDepth = agentIt->second.subagentLimits.maxDepth;
        }

        systemPrompt += ContextBuilder::buildSubagentContext(depth, maxDepth);
    }

    // create a modified message with effective content
    auto effectiveMessage = message;
    effectiveMessage.content = effectiveContent;

    // process message with event broadcasting
    // wrap in try-catch to ensure cleanup (clearActiveTurn, setActiveTurnHandle)
    // always runs, even if processMessage throws from pre-loop code
    try
    {
        loop->processMessage(effectiveMessage, systemPrompt, [this, sessionKey, targetAgent, subagentRunId](const AgentEvent &event)
                             {
            nlohmann::json eventData = event.data;
            eventData["agent"] = targetAgent;

            if (!eventData.contains("chat_id"))
            {
                eventData["chat_id"] = sessionKey;
            }

            dispatcher->broadcast(event.type, eventData);

            // update subagent progress with streamed content
            if (!subagentRunId.empty() && subagentRegistry && event.type == "chat:stream"
                && event.data.contains("content") && event.data["content"].is_string())
            {
                subagentRegistry->updateProgress(subagentRunId, event.data["content"].get<std::string>());
            } });
    }
    catch (const std::exception &ex)
    {
        clearActiveTurn(sessionKey);
        loop->setActiveTurnHandle(nullptr);

        // mark subagent as errored if this was a subagent run
        if (!subagentRunId.empty() && subagentRegistry)
        {
            handleSubagentCompletion(subagentRunId, std::string("Error: ") + ex.what(), true);
        }

        throw;
    }
    catch (...)
    {
        clearActiveTurn(sessionKey);
        loop->setActiveTurnHandle(nullptr);

        // mark subagent as errored if this was a subagent run
        if (!subagentRunId.empty() && subagentRegistry)
        {
            handleSubagentCompletion(subagentRunId, "Unknown error", true);
        }

        throw;
    }

    // clear active turn
    clearActiveTurn(sessionKey);

    // clear stale turn handle references
    loop->setActiveTurnHandle(nullptr);

    // fire MessageSent + AgentTurnEnd hooks
    if (hookRunner)
    {
        HookContext hookCtx;
        hookCtx.agentName = targetAgent;
        hookCtx.sessionKey = sessionKey;
        hookRunner->run(HookPoint::MessageSent, hookCtx);
        hookRunner->run(HookPoint::AgentTurnEnd, hookCtx);
    }

    // handle subagent completion
    if (!subagentRunId.empty() && subagentRegistry)
    {
        auto lastHistory = sessionManager->getHistory(sessionKey, 1);
        std::string outcome;

        if (!lastHistory.empty() && lastHistory.back().role == "assistant")
        {
            outcome = lastHistory.back().content;
        }

        handleSubagentCompletion(subagentRunId, outcome, false);
    }

    // drain followup/collect queue after turn completes
    drainSessionQueue(sessionKey);
}

// handle interrupt: abort current turn, clear queue, re-publish message
void Orchestrator::handleInterrupt(const std::string &sessionKey, const ionclaw::bus::InboundMessage &message)
{
    spdlog::info("[Orchestrator] Interrupt for session {}", sessionKey);

    // set abort flag on the active turn
    auto activeTurn = getActiveTurn(sessionKey);

    if (activeTurn)
    {
        activeTurn->aborted.store(true);
    }

    // clear all pending queue items for this session
    auto cleared = sessionQueue_->clear(sessionKey);

    if (cleared > 0)
    {
        spdlog::info("[Orchestrator] Cleared {} queued items for interrupted session {}", cleared, sessionKey);
    }

    // re-publish the interrupt message as a normal message
    // it will be picked up after the current turn exits
    ionclaw::bus::InboundMessage normalMsg = message;
    normalMsg.queueMode = std::nullopt;
    normalMsg.metadata["interrupt_requeued"] = true;
    bus->publishInbound(normalMsg);
}

// drain followup/collect queue after a turn completes
void Orchestrator::drainSessionQueue(const std::string &sessionKey)
{
    while (sessionQueue_->hasPending(sessionKey))
    {
        // check for interrupt before draining
        if (sessionQueue_->hasInterrupt(sessionKey))
        {
            spdlog::debug("[Orchestrator] Interrupt detected in queue, stopping drain for {}", sessionKey);
            break;
        }

        // check dropped messages
        auto dropped = sessionQueue_->droppedCount(sessionKey);
        auto summaryLines = sessionQueue_->droppedSummaryLines(sessionKey);
        sessionQueue_->resetDroppedState(sessionKey);

        auto items = sessionQueue_->drainFollowup(sessionKey);

        if (items.empty())
        {
            break;
        }

        // determine mode from first item
        auto mode = items.front().mode;

        if (mode == ionclaw::bus::QueueMode::Collect)
        {
            // wait for debounce
            auto settings = ionclaw::bus::resolveQueueSettings(
                config, items.front().message.channel);

            sessionQueue_->waitDebounce(sessionKey, settings.debounceMs);

            // re-drain after debounce (more items may have arrived)
            auto moreItems = sessionQueue_->drainFollowup(sessionKey);
            items.insert(items.end(), moreItems.begin(), moreItems.end());

            if (items.empty())
            {
                break;
            }

            // build collected prompt
            auto collectPrompt = ionclaw::bus::SessionQueue::buildCollectPrompt(items);

            // append overflow summary if items were dropped
            if (dropped > 0)
            {
                collectPrompt = ionclaw::bus::SessionQueue::buildSummaryPrompt(dropped, summaryLines) +
                                "\n\n" + collectPrompt;
            }

            // create synthetic InboundMessage with the collected content
            ionclaw::bus::InboundMessage synthetic;
            synthetic.channel = items.front().message.channel;
            synthetic.chatId = items.front().message.chatId;
            synthetic.senderId = items.front().message.senderId;
            synthetic.content = collectPrompt;
            synthetic.metadata = {{"synthetic", true}, {"queue_mode", "collect"}};

            processMessageDirect(synthetic);
        }
        else
        {
            // followup: process each item as a separate turn
            for (auto &item : items)
            {
                if (sessionQueue_->hasInterrupt(sessionKey))
                {
                    break;
                }

                processMessageDirect(item.message);
            }
        }
    }
}

void Orchestrator::handleSubagentCompletion(const std::string &runId, const std::string &outcome, bool errored)
{
    static constexpr int GRACE_PERIOD_SECONDS = 15;

    if (!subagentRegistry)
    {
        return;
    }

    auto record = subagentRegistry->getRecord(runId);

    if (record.runId.empty())
    {
        return;
    }

    if (errored && !record.createdAt.empty())
    {
        auto now = ionclaw::util::TimeHelper::now();
        auto elapsed = ionclaw::util::TimeHelper::diffSeconds(record.createdAt, now);

        if (elapsed < GRACE_PERIOD_SECONDS)
        {
            spdlog::warn("[Orchestrator] Subagent {} errored within {}s of creation (grace period: {}s), possible startup failure",
                         runId, elapsed, GRACE_PERIOD_SECONDS);
        }
    }

    auto status = errored ? SubagentStatus::Errored : SubagentStatus::Completed;
    subagentRegistry->updateStatus(runId, status, outcome);

    if (announceQueue)
    {
        auto statusStr = errored ? "errored" : "completed";
        auto announceMsg = "[Subagent run " + runId + " " + statusStr + "]\n" +
                           "Task: " + record.task + "\n" +
                           "Result: " + (outcome.empty() ? "(no output)" : outcome);

        auto announceId = AnnounceQueue::buildAnnounceId(record.childSessionKey, runId);
        announceQueue->enqueue(runId, record.requesterSessionKey, announceMsg, announceId);
    }

    if (hookRunner)
    {
        HookContext hookCtx;
        hookCtx.sessionKey = record.requesterSessionKey;
        hookCtx.data = {
            {"run_id", runId},
            {"status", errored ? "errored" : "completed"},
            {"outcome", outcome},
        };
        hookRunner->run(HookPoint::SubagentEnded, hookCtx);
    }

    // wake parent if all children are terminal
    if (subagentRegistry->allChildrenTerminal(record.requesterSessionKey))
    {
        spdlog::info("[Orchestrator] All children settled for session {}, waking parent", record.requesterSessionKey);

        std::string channel = "web";
        std::string chatId = record.requesterSessionKey;
        auto colonPos = record.requesterSessionKey.find(':');

        if (colonPos != std::string::npos)
        {
            channel = record.requesterSessionKey.substr(0, colonPos);
            chatId = record.requesterSessionKey.substr(colonPos + 1);
        }

        ionclaw::bus::InboundMessage wakeMsg;
        wakeMsg.channel = channel;
        wakeMsg.chatId = chatId;
        wakeMsg.content = "All spawned subagent tasks have completed. Review the results above and continue.";
        wakeMsg.queueMode = std::make_optional(ionclaw::bus::QueueMode::Followup);
        wakeMsg.metadata = {
            {"synthetic", true},
            {"wake_reason", "all_children_settled"},
        };

        bus->publishInbound(wakeMsg);
    }
}

// stop all agent loops and join worker thread
void Orchestrator::stop()
{
    if (!running.load())
    {
        return;
    }

    spdlog::info("[Orchestrator] Stopping...");

    running.store(false);

    // stop all agent loops
    for (auto &[name, loop] : agentLoops)
    {
        loop->stop();
    }

    // join worker thread
    if (workerThread.joinable())
    {
        workerThread.join();
    }

    // release all resources
    agentLoops.clear();
    providers.clear();
    contextBuilders.clear();
    skillsLoaders.clear();
    classifier.reset();
    hookRunner.reset();
    subagentRegistry.reset();
    announceQueue.reset();
    sessionQueue_.reset();

    spdlog::info("[Orchestrator] Stopped");
}

void Orchestrator::restart(const ionclaw::config::Config &newConfig)
{
    spdlog::info("[Orchestrator] Restarting with updated config...");

    stop();

    config = newConfig;

    start();

    spdlog::info("[Orchestrator] Restart complete");
}

std::vector<nlohmann::json> Orchestrator::getToolDefinitions() const
{
    return toolRegistry->getOpenAiDefinitions();
}

std::vector<nlohmann::json> Orchestrator::getFlatToolDefinitions() const
{
    return toolRegistry->getFlatDefinitions();
}

std::vector<std::string> Orchestrator::getAgentNames() const
{
    std::vector<std::string> names;
    names.reserve(config.agents.size());

    for (const auto &[name, agentConfig] : config.agents)
    {
        names.push_back(name);
    }

    return names;
}

} // namespace agent
} // namespace ionclaw
