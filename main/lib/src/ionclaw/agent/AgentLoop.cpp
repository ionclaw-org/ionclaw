#include "ionclaw/agent/AgentLoop.hpp"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <map>
#include <regex>
#include <set>
#include <sstream>

#include <fstream>
#include <thread>

#include "ionclaw/agent/Compaction.hpp"
#include "ionclaw/agent/ContextWindow.hpp"
#include "ionclaw/agent/HookRunner.hpp"
#include "ionclaw/agent/Orchestrator.hpp"
#include "ionclaw/agent/ToolLoopDetector.hpp"
#include "ionclaw/provider/ProviderHelper.hpp"
#include "ionclaw/tool/builtin/MemorySaveTool.hpp"
#include "ionclaw/transcription/TranscriptionProviderRegistry.hpp"
#include "ionclaw/util/StringHelper.hpp"
#include "ionclaw/util/TimeHelper.hpp"
#include "ionclaw/util/UniqueId.hpp"
#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace agent
{

// graduated thinking level downgrade: high→medium→low→(empty = remove)
std::string pickFallbackThinkingLevel(const std::string &current)
{
    if (current == "high")
        return "medium";
    if (current == "medium")
        return "low";
    return "";
}

// truncate text with ellipsis if exceeding max length
std::string AgentLoop::truncateText(const std::string &s, size_t maxLen)
{
    if (s.size() <= maxLen)
    {
        return s;
    }
    return ionclaw::util::StringHelper::utf8SafeTruncate(s, maxLen) + "...";
}

// strip incomplete trailing directive markers like [[...]]
std::string AgentLoop::stripTrailingDirectives(const std::string &text)
{
    static thread_local const std::regex directivePattern(R"(\[\[[^\]]*\]?\]?\s*$)");
    return std::regex_replace(text, directivePattern, "");
}

// estimate total byte size of messages for prompt size validation
size_t AgentLoop::estimatePromptBytes(const std::vector<ionclaw::provider::Message> &messages)
{
    size_t total = 0;

    for (const auto &msg : messages)
    {
        total += msg.content.size();
        total += msg.reasoningContent.size();

        for (const auto &tc : msg.toolCalls)
        {
            total += tc.arguments.dump().size();
        }

        if (msg.contentBlocks.is_array())
        {
            total += msg.contentBlocks.dump().size();
        }
    }

    return total;
}

// format a human-readable summary for a tool call
std::string AgentLoop::formatToolSummary(const std::string &name, const nlohmann::json &args)
{
    const size_t maxSummary = 200;

    // helper to extract string argument with fallback
    auto str = [&args](const char *key, const std::string &fallback = "") -> std::string
    {
        if (!args.contains(key) || !args[key].is_string())
        {
            return fallback;
        }
        return args[key].get<std::string>();
    };

    // match tool name to summary format
    if (name == "http_client")
    {
        std::string method = str("method", "GET");
        std::string url = str("url");
        return truncateText(method + " " + url, maxSummary);
    }
    if (name == "web_search")
    {
        return "Search: " + truncateText(str("query"), maxSummary - 9);
    }
    if (name == "generate_image")
    {
        return "Generate: " + truncateText(str("prompt"), maxSummary - 12);
    }
    if (name == "image_ops")
    {
        return "Image ops: " + truncateText(str("operation"), maxSummary - 14);
    }
    if (name == "exec")
    {
        return "Run: " + truncateText(str("command"), maxSummary - 6);
    }
    if (name == "read_file")
    {
        return "Read: " + truncateText(str("path"), maxSummary - 7);
    }
    if (name == "write_file")
    {
        return "Write: " + truncateText(str("path"), maxSummary - 8);
    }
    if (name == "edit_file")
    {
        return "Edit: " + truncateText(str("path"), maxSummary - 7);
    }
    if (name == "list_dir")
    {
        return "List: " + truncateText(str("path"), maxSummary - 7);
    }
    if (name == "memory_read")
    {
        return "Memory: " + truncateText(str("file"), maxSummary - 9);
    }
    if (name == "memory_save")
    {
        return "Save memory";
    }
    if (name == "memory_search")
    {
        return "Search memory: " + truncateText(str("query"), maxSummary - 17);
    }
    if (name == "message")
    {
        std::string content = str("content");
        if (content.empty())
        {
            return "Send message";
        }
        return "Send: " + truncateText(content, maxSummary - 7);
    }
    if (name == "web_fetch")
    {
        return "Fetch: " + truncateText(str("url"), maxSummary - 8);
    }
    if (name == "rss_reader")
    {
        return "RSS: " + truncateText(str("url"), maxSummary - 6);
    }
    if (name == "browser")
    {
        std::string action = str("action");
        if (action == "navigate" && args.contains("url") && args["url"].is_string())
        {
            return "Browser: " + truncateText(args["url"].get<std::string>(), maxSummary - 11);
        }
        return "Browser: " + truncateText(action, maxSummary - 11);
    }
    if (name == "spawn")
    {
        return "Task: " + truncateText(str("task"), maxSummary - 7);
    }
    if (name == "cron")
    {
        return "Cron: " + truncateText(str("action"), maxSummary - 7);
    }

    // fallback: first key-value or empty
    if (args.is_object() && !args.empty())
    {
        auto it = args.begin();
        if (it.value().is_string())
        {
            return truncateText(it.key() + ": " + it.value().get<std::string>(), maxSummary);
        }
    }
    return "";
}

// resolve media file paths to base64 content blocks for the LLM
nlohmann::json AgentLoop::resolveMedia(const std::vector<std::string> &paths, const std::string &projectRoot)
{
    namespace fs = std::filesystem;

    static const std::set<std::string> AUDIO_EXTENSIONS = {
        ".mp3", ".wav", ".ogg", ".oga", ".opus", ".m4a", ".webm", ".aac", ".flac"};

    nlohmann::json blocks = nlohmann::json::array();

    for (const auto &path : paths)
    {
        // only process audio files for transcription; images are handled via vision tool
        auto ext = fs::path(path).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c)
                       { return c < 0x80 ? static_cast<unsigned char>(std::tolower(c)) : c; });

        if (AUDIO_EXTENSIONS.count(ext) == 0)
        {
            continue;
        }

        auto fullPath = projectRoot + "/" + path;

        if (!fs::exists(fullPath) || !fs::is_regular_file(fullPath))
        {
            spdlog::warn("media file not found: {}", fullPath);
            continue;
        }

        // audio: transcribe if configured
        auto format = ext.substr(1); // strip leading dot

        if (configPtr && !configPtr->transcription.model.empty())
        {
            auto model = configPtr->transcription.model;
            auto slashPos = model.find('/');
            auto providerName = slashPos != std::string::npos ? model.substr(0, slashPos) : model;

            auto *provider = ionclaw::transcription::TranscriptionProviderRegistry::instance().get(providerName);

            if (!provider)
            {
                spdlog::warn("[transcription] no provider found for '{}', skipping audio", providerName);
                blocks.push_back({{"type", "warning"}, {"text", "Transcription provider '" + providerName + "' not found. Audio was ignored."}});
                continue;
            }

            // read raw bytes
            std::ifstream f(fullPath, std::ios::binary);

            if (!f.is_open())
            {
                spdlog::warn("[transcription] failed to open: {}", fullPath);
                continue;
            }

            std::string audioData((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            f.close();

            if (audioData.empty())
            {
                spdlog::warn("[transcription] empty audio file: {}", fullPath);
                continue;
            }

            ionclaw::transcription::TranscriptionContext ctx;
            ctx.model = model;
            ctx.providerName = providerName;
            ctx.config = configPtr;

            try
            {
                auto result = provider->transcribe(audioData, format, ctx);

                if (!result.text.empty())
                {
                    blocks.push_back({{"type", "transcription"}, {"text", result.text}});
                }
                else
                {
                    spdlog::warn("[transcription] empty result for: {}", fullPath);
                }
            }
            catch (const std::exception &e)
            {
                spdlog::error("[transcription] error: {}", e.what());
            }
        }
        else
        {
            spdlog::warn("[transcription] no model configured, skipping audio: {}", fullPath);
            blocks.push_back({{"type", "warning"}, {"text", "No transcription model configured. Audio was ignored. Configure it in Settings."}});
        }
    }

    spdlog::debug("[resolveMedia] resolved {} block(s)", blocks.size());
    return blocks;
}

// usage tracker

// accumulate token usage from a response
void UsageTracker::record(const nlohmann::json &usage)
{
    if (!usage.is_object())
    {
        return;
    }

    auto pt = std::max(0, usage.value("prompt_tokens", 0));
    auto ct = std::max(0, usage.value("completion_tokens", 0));
    auto tt = std::max(0, usage.value("total_tokens", 0));

    // cache tokens (anthropic: cache_read_input_tokens / cache_creation_input_tokens)
    auto crt = std::max(0, usage.value("cache_read_input_tokens", 0));
    auto cwt = std::max(0, usage.value("cache_creation_input_tokens", 0));

    // last call (overwritten each time)
    lastCallPromptTokens = pt;
    lastCallCompletionTokens = ct;
    lastCallTotalTokens = tt;
    lastCallCacheReadTokens = crt;
    lastCallCacheWriteTokens = cwt;

    // accumulated
    promptTokens += pt;
    completionTokens += ct;
    totalTokens += tt;
    cacheReadTokens += crt;
    cacheWriteTokens += cwt;
}

nlohmann::json UsageTracker::toJson() const
{
    return {
        {"prompt_tokens", promptTokens},
        {"completion_tokens", completionTokens},
        {"total_tokens", totalTokens},
        {"cache_read_tokens", cacheReadTokens},
        {"cache_write_tokens", cacheWriteTokens},
        {"last_call_prompt_tokens", lastCallPromptTokens},
        {"last_call_completion_tokens", lastCallCompletionTokens},
        {"last_call_total_tokens", lastCallTotalTokens},
        {"last_call_cache_read_tokens", lastCallCacheReadTokens},
        {"last_call_cache_write_tokens", lastCallCacheWriteTokens},
    };
}

// agent loop

AgentLoop::AgentLoop(
    std::shared_ptr<ionclaw::provider::LlmProvider> provider,
    std::shared_ptr<ionclaw::tool::ToolRegistry> toolRegistry,
    std::shared_ptr<ionclaw::session::SessionManager> sessionManager,
    std::shared_ptr<ionclaw::task::TaskManager> taskManager,
    std::shared_ptr<ionclaw::bus::EventDispatcher> dispatcher,
    const ionclaw::config::AgentConfig &agentConfig,
    const std::string &agentName)
    : provider(std::move(provider))
    , toolRegistry(std::move(toolRegistry))
    , sessionManager(std::move(sessionManager))
    , taskManager(std::move(taskManager))
    , dispatcher(std::move(dispatcher))
    , agentConfig(agentConfig)
    , agentName(agentName)
{
}

void AgentLoop::processMessage(
    const ionclaw::bus::InboundMessage &message,
    const std::string &systemPrompt,
    AgentEventCallback callback)
{
    auto sessionKey = message.sessionKey();

    // reset per-turn state to prevent cross-session leaks
    lastSentContent.clear();
    compactionCount = 0;
    memoryFlushCompactionCount = 0;

    // get task id from metadata
    std::string taskId;

    if (message.metadata.contains("task_id") && message.metadata["task_id"].is_string())
    {
        taskId = message.metadata["task_id"].get<std::string>();
    }

    // update task state to DOING
    if (!taskId.empty())
    {
        try
        {
            taskManager->updateState(taskId, ionclaw::task::TaskState::Doing);
            taskManager->setAgent(taskId, agentName);
        }
        catch (const std::exception &e)
        {
            spdlog::warn("[AgentLoop] Failed to update task state: {}", e.what());
        }
    }

    // check if the channel already saved and broadcast the message
    const bool messageSaved = message.metadata.contains("message_saved") &&
                              message.metadata["message_saved"].is_boolean() &&
                              message.metadata["message_saved"].get<bool>();

    // broadcast user message for non-web channels (skip when the channel already broadcast it)
    if (message.channel != "web" && !messageSaved)
    {
        dispatcher->broadcast("chat:user_message", {
                                                       {"chat_id", message.chatId},
                                                       {"content", message.content},
                                                       {"channel", message.channel},
                                                   });
    }

    // ensure session exists
    sessionManager->ensureSession(sessionKey);

    auto content = message.content;

    // handle /new command
    if (content == "/new")
    {
        sessionManager->clearSession(sessionKey);
        dispatcher->broadcast("sessions:updated", nlohmann::json::object());

        if (!taskId.empty())
        {
            try
            {
                taskManager->updateState(taskId, ionclaw::task::TaskState::Done);
            }
            catch (...)
            {
            }
        }

        AgentEvent endEvent;
        endEvent.type = "chat:message";
        endEvent.data = {
            {"chat_id", sessionKey},
            {"content", "Conversation cleared. How can I help you?"},
            {"agent_name", agentName},
        };

        if (!taskId.empty())
        {
            endEvent.data["task_id"] = taskId;
        }

        callback(endEvent);
        return;
    }

    // handle /help command
    if (content == "/help")
    {
        if (!taskId.empty())
        {
            try
            {
                taskManager->updateState(taskId, ionclaw::task::TaskState::Done);
            }
            catch (...)
            {
            }
        }

        auto helpText = "Available commands:\n  /new - Start a new conversation\n  /help - Show this help message\n";

        AgentEvent endEvent;
        endEvent.type = "chat:message";
        endEvent.data = {
            {"chat_id", sessionKey},
            {"content", helpText},
            {"agent_name", agentName},
        };

        if (!taskId.empty())
        {
            endEvent.data["task_id"] = taskId;
        }

        callback(endEvent);
        return;
    }

    // create tool context
    ionclaw::tool::ToolContext toolContext;
    toolContext.agentName = agentName;
    toolContext.sessionKey = sessionKey;

    if (!agentConfig.workspace.empty())
    {
        toolContext.workspacePath = agentConfig.workspace;
    }

    toolContext.publicPath = publicPath;

    // wrap messageSender to capture content delivered via message tool
    // (used as response fallback when model returns empty after tool execution)
    std::string messageToolDeliveredContent;

    if (messageSender)
    {
        toolContext.messageSender = [this, &messageToolDeliveredContent](const std::string &channel, const std::string &chatId, const std::string &content)
        {
            messageToolDeliveredContent = content;
            messageSender(channel, chatId, content);
        };
    }
    toolContext.config = configPtr;
    toolContext.taskManager = taskManager.get();
    toolContext.sessionManager = sessionManager.get();
    toolContext.bus = busPtr;
    toolContext.dispatcher = dispatcher.get();
    toolContext.cronService = cronServicePtr;
    toolContext.subagentRegistry = subagentRegistryPtr;
    toolContext.hookRunner = hookRunnerPtr;

    // resolve media paths to base64 content blocks (and transcriptions)
    // must happen before saving user message so transcription text is persisted
    std::string projectRoot = configPtr ? configPtr->projectPath : "";
    auto mediaBlocks = resolveMedia(message.media, projectRoot);

    // extract transcription blocks and prepend text to user content
    // images are NOT embedded as content blocks — the model uses the vision tool to analyze them
    std::string effectiveContent = content;

    for (const auto &block : mediaBlocks)
    {
        auto type = block.value("type", "");

        if (type == "transcription")
        {
            auto text = block.value("text", "");

            if (!text.empty())
            {
                // prepend transcription to user message
                if (effectiveContent.empty())
                {
                    effectiveContent = text;
                }
                else
                {
                    effectiveContent = "[Audio transcription]: " + text + "\n\n" + effectiveContent;
                }

                // broadcast transcription event to web client
                dispatcher->broadcast("chat:transcription", {
                                                                {"content", text},
                                                                {"chat_id", message.chatId},
                                                                {"agent_name", agentName},
                                                            });
            }
        }
        else if (type == "warning")
        {
            dispatcher->broadcast("chat:warning", {
                                                      {"content", block.value("text", "")},
                                                      {"chat_id", message.chatId},
                                                      {"agent_name", agentName},
                                                  });
        }
    }

    // save user message to session (skip if already persisted by channel)
    if (!messageSaved)
    {
        ionclaw::session::SessionMessage userSessionMsg;
        userSessionMsg.role = "user";
        userSessionMsg.content = effectiveContent;
        userSessionMsg.timestamp = ionclaw::util::TimeHelper::now();
        userSessionMsg.agentName = agentName;

        for (const auto &path : message.media)
        {
            userSessionMsg.media.push_back(path);
        }

        sessionManager->addMessage(sessionKey, userSessionMsg);
        dispatcher->broadcast("sessions:updated", nlohmann::json::object());
    }
    else if (effectiveContent != content)
    {
        // web channel: message was already saved by ChatRoutes with raw content;
        // update the last message in session to include transcription text
        sessionManager->updateLastMessageContent(sessionKey, effectiveContent);
        sessionManager->save(sessionKey);
    }

    // build messages from session history + current user message
    auto history = sessionManager->getHistory(sessionKey, agentConfig.agentParams.maxHistory);

    // handle abort recovery: skip messages that were in flight during last crash
    {
        auto session = sessionManager->getOrCreate(sessionKey);

        if (session.abortedLastRun && session.abortCutoffMessageIndex >= 0)
        {
            auto cutoff = session.abortCutoffMessageIndex;

            if (cutoff < static_cast<int>(history.size()))
            {
                history.erase(history.begin() + cutoff, history.end());
                spdlog::info("[AgentLoop] Trimmed {} post-abort messages from session {}", static_cast<int>(session.messages.size()) - cutoff, sessionKey);
            }

            sessionManager->clearAbortFlag(sessionKey);
        }
    }

    // exclude the last user message from history (we add it separately)
    if (!history.empty() && history.back().role == "user")
    {
        history.pop_back();
    }

    // prepend working directory path to user message for context
    if (!agentConfig.workspace.empty())
    {
        effectiveContent = "[cwd: " + agentConfig.workspace + "]\n" + effectiveContent;
    }

    // strip incomplete trailing directive markers
    effectiveContent = stripTrailingDirectives(effectiveContent);

    // append media annotations for the current message (e.g. "[image attached: path — use vision tool...]")
    if (!message.media.empty())
    {
        std::vector<nlohmann::json> mediaPaths;
        mediaPaths.reserve(message.media.size());

        for (const auto &p : message.media)
        {
            mediaPaths.push_back(p);
        }

        auto annotation = ContextBuilder::buildMediaAnnotation(mediaPaths);

        if (!annotation.empty())
        {
            effectiveContent += annotation;
        }
    }

    // images are not embedded — the model uses the vision tool via file paths in annotations
    nlohmann::json emptyBlocks = nlohmann::json::array();
    std::map<int, nlohmann::json> emptyHistoryMedia;

    auto messages = ContextBuilder::buildMessages(systemPrompt, history, effectiveContent, emptyBlocks, emptyHistoryMedia);

    // prompt size validation: defense-in-depth against oversized payloads
    auto promptBytes = estimatePromptBytes(messages);

    if (promptBytes > MAX_PROMPT_BYTES)
    {
        spdlog::warn("[AgentLoop] Prompt size {}B exceeds limit {}B, compacting", promptBytes, MAX_PROMPT_BYTES);
        compactWithHooks(messages, sessionKey, taskId, agentConfig.modelParams);
    }

    // run agent loop
    try
    {
        auto [responseText, responseBlocks] = runAgentLoop(messages, taskId, sessionKey, sessionKey, agentName, toolContext, callback);

        // resolve empty/silent responses: prefer message-tool content from this turn,
        // then fall back to last sent content from a previous turn
        if (responseText == "[SILENT]")
        {
            responseText = !messageToolDeliveredContent.empty() ? messageToolDeliveredContent
                           : !lastSentContent.empty()           ? lastSentContent
                                                                : "";
        }
        else if (responseText.empty() && !messageToolDeliveredContent.empty())
        {
            responseText = messageToolDeliveredContent;
        }

        // track last sent content for future silent-reply fallback
        if (!responseText.empty() && responseText != "[SILENT]")
        {
            lastSentContent = responseText;
        }

        // store content as typed blocks array
        nlohmann::json contentBlocks = responseBlocks.empty()
                                           ? nlohmann::json::array({{{"type", "text"}, {"text", responseText}}})
                                           : nlohmann::json(responseBlocks);

        // save assistant response to session
        ionclaw::session::SessionMessage assistantSessionMsg;
        assistantSessionMsg.role = "assistant";
        assistantSessionMsg.content = responseText;
        assistantSessionMsg.timestamp = ionclaw::util::TimeHelper::now();
        assistantSessionMsg.agentName = agentName;
        sessionManager->addMessage(sessionKey, assistantSessionMsg);

        dispatcher->broadcast("sessions:updated", nlohmann::json::object());

        // publish outbound for non-WebSocket channels
        if (busPtr)
        {
            ionclaw::bus::OutboundMessage outbound;
            outbound.channel = message.channel;
            outbound.chatId = message.chatId;
            outbound.content = responseText;
            outbound.metadata = {
                {"task_id", taskId},
                {"agent_name", agentName},
                {"content_blocks", contentBlocks},
            };

            if (message.metadata.contains("reply_to_message_id"))
            {
                outbound.metadata["reply_to_message_id"] = message.metadata["reply_to_message_id"];
            }

            busPtr->publishOutbound(outbound);
        }

        // update task to done
        if (!taskId.empty())
        {
            try
            {
                auto resultPreview = ionclaw::util::StringHelper::utf8SafeTruncate(responseText, TASK_RESULT_MAX_CHARS);
                taskManager->updateState(taskId, ionclaw::task::TaskState::Done, resultPreview);
            }
            catch (const std::exception &e)
            {
                spdlog::warn("[AgentLoop] Failed to update task to done: {}", e.what());
            }
        }

        // emit final message event
        AgentEvent endEvent;
        endEvent.type = "chat:message";
        endEvent.data = {
            {"chat_id", sessionKey},
            {"content", responseText},
            {"agent_name", agentName},
        };

        if (!taskId.empty())
        {
            endEvent.data["task_id"] = taskId;
        }

        callback(endEvent);
    }
    catch (const std::exception &e)
    {
        spdlog::error("[AgentLoop] Agent loop error for task {}: {}", taskId, e.what());

        auto errorCategory = ionclaw::provider::ProviderHelper::classifyError(e.what());
        std::string errorText;

        if (errorCategory == "host_not_found")
        {
            auto slashPos = agentConfig.model.find('/');
            auto providerName = slashPos != std::string::npos ? agentConfig.model.substr(0, slashPos) : agentConfig.model;
            errorText = "Could not connect to provider '" + providerName + "': the host was not found. "
                                                                           "Please check that the provider's base_url is correct and the service is reachable. "
                                                                           "(model: " +
                        agentConfig.model + ")";
        }
        else if (errorCategory == "auth")
        {
            errorText = "Authentication failed for model '" + agentConfig.model + "'. "
                                                                                  "Please check that the API key is valid and has the required permissions.";
        }
        else if (errorCategory == "model_not_found")
        {
            errorText = "Model '" + agentConfig.model + "' was not found by the provider. "
                                                        "Please check the model name in the agent configuration.";
        }
        else
        {
            errorText = std::string("I encountered an error: ") + e.what();
        }

        // save error response to session
        ionclaw::session::SessionMessage errorSessionMsg;
        errorSessionMsg.role = "assistant";
        errorSessionMsg.content = errorText;
        errorSessionMsg.timestamp = ionclaw::util::TimeHelper::now();
        errorSessionMsg.agentName = agentName;
        sessionManager->addMessage(sessionKey, errorSessionMsg);

        dispatcher->broadcast("sessions:updated", nlohmann::json::object());

        if (!taskId.empty())
        {
            try
            {
                taskManager->setError(taskId, e.what());
                taskManager->updateState(taskId, ionclaw::task::TaskState::Error);
            }
            catch (...)
            {
            }
        }

        // broadcast error to client
        AgentEvent errorEvent;
        errorEvent.type = "chat:message";
        errorEvent.data = {
            {"chat_id", sessionKey},
            {"content", errorText},
            {"error", e.what()},
            {"agent_name", agentName},
        };

        if (!taskId.empty())
        {
            errorEvent.data["task_id"] = taskId;
        }

        callback(errorEvent);

        AgentEvent streamEndEvent;
        streamEndEvent.type = "chat:stream_end";
        streamEndEvent.data = {
            {"chat_id", sessionKey},
            {"agent_name", agentName},
        };

        if (!taskId.empty())
        {
            streamEndEvent.data["task_id"] = taskId;
        }

        callback(streamEndEvent);
    }
}

std::pair<std::string, std::vector<nlohmann::json>> AgentLoop::runAgentLoop(
    std::vector<ionclaw::provider::Message> &messages,
    const std::string &taskId,
    const std::string &chatId,
    const std::string &sessionKey,
    const std::string &effectiveName,
    const ionclaw::tool::ToolContext &toolContext,
    AgentEventCallback &callback)
{
    auto maxIterations = agentConfig.agentParams.maxIterations;

    // resolve per-channel history limit (session key format: "channel:chatId")
    auto maxHistory = agentConfig.agentParams.maxHistory;
    {
        auto colonPos = sessionKey.find(':');
        if (colonPos != std::string::npos)
        {
            auto channelPrefix = sessionKey.substr(0, colonPos);
            auto it = agentConfig.agentParams.channelHistoryLimits.find(channelPrefix);
            if (it != agentConfig.agentParams.channelHistoryLimits.end())
            {
                maxHistory = it->second;
            }
        }
    }

    ToolLoopDetector loopDetector;
    UsageTracker usageTracker;
    std::vector<nlohmann::json> blocks;
    recentToolFingerprints.clear();
    int contextOverflowAttempts = 0;
    bool transientRetried = false;
    static constexpr int MAX_OVERFLOW_COMPACTION_ATTEMPTS = 3;

    // per-turn copy of model params so thinking downgrades don't persist across turns
    nlohmann::json turnModelParams = agentConfig.modelParams;

    for (int iteration = 0; iteration < maxIterations; ++iteration)
    {
        if (stopped.load())
        {
            spdlog::info("[AgentLoop] Stopped during iteration {}", iteration);
            break;
        }

        // check per-turn abort (from interrupt)
        if (activeTurnHandle && activeTurnHandle->aborted.load())
        {
            spdlog::info("[AgentLoop] Turn aborted by interrupt at iteration {}", iteration);
            break;
        }

        // poll for steer messages and inject as user messages between iterations
        if (sessionQueuePtr && iteration > 0)
        {
            auto steerItems = sessionQueuePtr->drainSteer(sessionKey);

            for (const auto &item : steerItems)
            {
                spdlog::info("[AgentLoop] Injecting steer message into turn (session: {})", sessionKey);

                ionclaw::provider::Message steerMsg;
                steerMsg.role = "user";
                steerMsg.content = item.message.content;
                messages.push_back(steerMsg);
            }
        }

        // track iteration for task progress
        if (!taskId.empty())
        {
            try
            {
                taskManager->incrementIteration(taskId);
            }
            catch (...)
            {
            }
        }

        // trim history if configured
        messages = ContextWindow::trimHistory(messages, maxHistory);

        // repair tool use/result pairing after trimming
        ContextBuilder::repairToolUseResultPairing(messages);

        // check minimum context guard (use turnModelParams so thinking downgrades affect limits)
        auto contextGuard = ContextWindow::checkMinContext(messages, agentConfig.model, turnModelParams, agentConfig.agentParams.contextTokens);

        if (contextGuard.level == ContextGuardLevel::Critical)
        {
            spdlog::warn("[AgentLoop] Context window critically low ({:.0f}% used, ~{} tokens remaining), forcing compaction",
                         contextGuard.usageRatio * 100, contextGuard.modelLimit - contextGuard.estimatedTokens);
            compactWithHooks(messages, sessionKey, taskId, turnModelParams, &toolContext);
        }
        else if (contextGuard.level == ContextGuardLevel::Warning)
        {
            spdlog::info("[AgentLoop] Context window usage high ({:.0f}%)", contextGuard.usageRatio * 100);
        }

        // compact context if approaching model limit
        if (ContextWindow::needsCompaction(messages, agentConfig.model, turnModelParams, agentConfig.agentParams.contextTokens))
        {
            spdlog::info("[AgentLoop] Context window near limit, compacting (task {}, iteration {})", taskId, iteration);

            // prune history to fit within 50% of context before summarizing
            messages = Compaction::pruneHistoryForContextShare(messages, agentConfig.model, turnModelParams);

            compactWithHooks(messages, sessionKey, taskId, turnModelParams, &toolContext);
        }

        // strip thinking from older messages to save context
        ContextBuilder::stripThinkingFromHistory(messages);

        // prune images from older user messages
        ContextBuilder::pruneHistoryImages(messages, 4);

        // enforce tool result budget: cap at 60% of model context (in chars ≈ tokens*4)
        auto modelLimit = ContextWindow::getModelLimit(agentConfig.model, turnModelParams, agentConfig.agentParams.contextTokens);
        auto toolResultBudget = static_cast<int>(modelLimit * 0.6 * 4);
        ContextBuilder::enforceToolResultBudget(messages, toolResultBudget);

        // signal typing to the frontend
        dispatcher->broadcast("chat:typing", {
                                                 {"chat_id", chatId},
                                                 {"agent_name", effectiveName},
                                                 {"task_id", taskId},
                                             });

        // consume stream with context overflow recovery
        StreamResult response;

        try
        {
            response = consumeStream(messages, taskId, chatId, effectiveName, usageTracker, callback, turnModelParams);
        }
        catch (const std::exception &e)
        {
            auto category = ionclaw::provider::ProviderHelper::classifyError(e.what());

            if (category == "context_overflow" && contextOverflowAttempts < MAX_OVERFLOW_COMPACTION_ATTEMPTS)
            {
                contextOverflowAttempts++;
                spdlog::warn("[AgentLoop] Context overflow from LLM (attempt {}/{}), recovering (task {})",
                             contextOverflowAttempts, MAX_OVERFLOW_COMPACTION_ATTEMPTS, taskId);

                if (contextOverflowAttempts > 1)
                {
                    // on subsequent attempts, truncate oversized tool results first
                    for (auto &msg : messages)
                    {
                        if (msg.role == "tool" && msg.content.size() > 2000)
                        {
                            auto head = ionclaw::util::StringHelper::utf8SafeTruncate(msg.content, 1000);

                            // UTF-8 safe tail: skip forward past continuation bytes
                            size_t tailStart = msg.content.size() - 500;
                            while (tailStart < msg.content.size() &&
                                   (static_cast<unsigned char>(msg.content[tailStart]) & 0xC0) == 0x80)
                            {
                                ++tailStart;
                            }
                            auto tail = msg.content.substr(tailStart);

                            msg.content = head + "\n[... tool output truncated for context recovery ...]\n" + tail;
                        }
                    }
                }

                compactWithHooks(messages, sessionKey, taskId, turnModelParams, &toolContext);
                --iteration;
                continue;
            }

            // role ordering: clear transcript and return user-facing message
            if (category == "role_ordering")
            {
                spdlog::warn("[AgentLoop] Role ordering error, clearing session (task {})", taskId);
                sessionManager->clearSession(sessionKey);
                dispatcher->broadcast("sessions:updated", nlohmann::json::object());
                return {"I encountered a conversation format error. The session has been reset — please try again.", blocks};
            }

            // thinking constraint: graduated downgrade (high→medium→low→off)
            if (category == "thinking_constraint" && turnModelParams.contains("thinking"))
            {
                auto current = turnModelParams["thinking"].is_string() ? turnModelParams["thinking"].get<std::string>() : "";
                auto fallback = pickFallbackThinkingLevel(current);

                if (fallback.empty())
                {
                    turnModelParams.erase("thinking");
                    spdlog::warn("[AgentLoop] Thinking constraint, removing thinking param (task {})", taskId);
                }
                else
                {
                    turnModelParams["thinking"] = fallback;
                    spdlog::warn("[AgentLoop] Thinking constraint, downgrading {} → {} (task {})", current, fallback, taskId);
                }

                --iteration;
                continue;
            }

            // timeout: downgrade thinking level to reduce latency
            if (category == "timeout" && turnModelParams.contains("thinking"))
            {
                auto current = turnModelParams["thinking"].is_string() ? turnModelParams["thinking"].get<std::string>() : "";
                auto fallback = pickFallbackThinkingLevel(current);

                if (!fallback.empty())
                {
                    turnModelParams["thinking"] = fallback;
                    spdlog::warn("[AgentLoop] Timeout with thinking, downgrading {} → {} (task {})", current, fallback, taskId);
                    --iteration;
                    continue;
                }
            }

            // transient HTTP: single retry with delay (independent of overflow counter)
            if (category == "transient" && !transientRetried)
            {
                transientRetried = true;
                spdlog::warn("[AgentLoop] Transient HTTP error, retrying once after delay (task {})", taskId);
                std::this_thread::sleep_for(std::chrono::milliseconds(2500));
                --iteration;
                continue;
            }

            throw;
        }

        // accumulate text block
        if (!response.content.empty())
        {
            blocks.push_back({{"type", "text"}, {"text", response.content}});
        }

        // handle token limit
        if (response.finishReason == "length")
        {
            spdlog::warn("[AgentLoop] LLM hit token limit at iteration {} (task {})", iteration, taskId);

            if (!taskId.empty() && usageTracker.totalTokens > 0)
            {
                try
                {
                    taskManager->setUsage(taskId, usageTracker.toJson());
                }
                catch (...)
                {
                }
            }

            auto text = response.content.empty()
                            ? "My response was cut short due to token limits."
                            : response.content;
            return {text, blocks};
        }

        if (!response.toolCalls.empty())
        {
            // record tool calls for loop detection
            for (const auto &tc : response.toolCalls)
            {
                loopDetector.recordCall(tc.name, tc.arguments.dump());
            }

            // check for loops before executing
            auto loopResult = loopDetector.check();

            if (loopResult.shouldStop)
            {
                spdlog::warn("[AgentLoop] {} at iteration {} (task {})", loopResult.reason, iteration, taskId);

                if (!taskId.empty() && usageTracker.totalTokens > 0)
                {
                    try
                    {
                        taskManager->setUsage(taskId, usageTracker.toJson());
                    }
                    catch (...)
                    {
                    }
                }

                auto text = response.content.empty()
                                ? "I seem to be repeating the same actions. Let me stop here."
                                : response.content;
                return {text, blocks};
            }

            if (loopResult.severity == LoopSeverity::Warning)
            {
                auto warningKey = loopResult.detector + ":" + response.toolCalls.back().name;

                if (loopDetector.shouldEmitWarning(warningKey, iteration))
                {
                    spdlog::warn("[AgentLoop] {}", loopResult.reason);
                }
            }

            // add assistant message with tool calls
            ContextBuilder::addAssistantMessage(
                messages, response.content, response.toolCalls, response.reasoningContent);

            // execute tools
            for (const auto &tc : response.toolCalls)
            {
                if (stopped.load())
                {
                    break;
                }

                // tool call deduplication: skip if identical call was already executed this turn
                auto fingerprint = tc.name + ":" + tc.arguments.dump();
                auto fpHash = ToolLoopDetector::hashString(fingerprint);

                if (recentToolFingerprints.count(fpHash) > 0)
                {
                    spdlog::debug("[AgentLoop] Skipping duplicate tool call: {}", tc.name);
                    ContextBuilder::addToolResult(messages, tc.id, tc.name, "[duplicate call skipped]");
                    continue;
                }

                recentToolFingerprints.insert(fpHash);

                // parse arguments if they are still a string
                nlohmann::json args = tc.arguments;

                if (args.is_string())
                {
                    try
                    {
                        args = nlohmann::json::parse(args.get<std::string>());
                    }
                    catch (...)
                    {
                    }
                }

                // fire BeforeToolCall hook (may block or modify params)
                if (hookRunnerPtr)
                {
                    HookContext hookCtx;
                    hookCtx.agentName = effectiveName;
                    hookCtx.sessionKey = sessionKey;
                    hookCtx.taskId = taskId;
                    hookCtx.data = {{"tool", tc.name}, {"arguments", args}};
                    hookRunnerPtr->run(HookPoint::BeforeToolCall, hookCtx);

                    if (hookCtx.blocked)
                    {
                        auto reason = hookCtx.blockReason.empty()
                                          ? "Tool call blocked by hook"
                                          : hookCtx.blockReason;
                        spdlog::info("[AgentLoop] Tool {} blocked by hook: {}", tc.name, reason);
                        ContextBuilder::addToolResult(messages, tc.id, tc.name, "Error: " + reason);
                        continue;
                    }

                    // allow hooks to modify parameters
                    if (hookCtx.data.contains("arguments") && hookCtx.data["arguments"] != args)
                    {
                        args = hookCtx.data["arguments"];
                    }
                }

                auto result = toolRegistry->executeTool(tc.name, args, toolContext);

                // fire AfterToolCall hook
                if (hookRunnerPtr)
                {
                    HookContext hookCtx;
                    hookCtx.agentName = effectiveName;
                    hookCtx.sessionKey = sessionKey;
                    hookCtx.taskId = taskId;
                    hookCtx.data = {
                        {"tool", tc.name},
                        {"arguments", args},
                        {"success", result.text.rfind("Error", 0) != 0},
                    };
                    hookRunnerPtr->run(HookPoint::AfterToolCall, hookCtx);
                }

                // record result hash for no-progress detection
                loopDetector.recordResult(tc.name, ToolLoopDetector::hashString(result.text));

                if (!taskId.empty())
                {
                    try
                    {
                        taskManager->incrementToolCount(taskId);
                    }
                    catch (...)
                    {
                    }
                }

                std::string toolSummary = formatToolSummary(tc.name, tc.arguments);

                // broadcast tool use event
                dispatcher->broadcast("chat:tool_use", {
                                                           {"task_id", taskId},
                                                           {"chat_id", chatId},
                                                           {"tool", tc.name},
                                                           {"description", toolSummary},
                                                           {"agent_name", effectiveName},
                                                       });

                blocks.push_back({
                    {"type", "tool_use"},
                    {"name", tc.name},
                    {"description", toolSummary},
                });

                // add tool result to messages (with optional media blocks)
                ContextBuilder::addToolResult(messages, tc.id, tc.name, result.text, result.media);
            }

            // flush synthetic error results for any tool calls that didn't get executed (abort mid-batch)
            if (stopped.load())
            {
                for (size_t ti = 0; ti < response.toolCalls.size(); ++ti)
                {
                    // check if this tool call already has a result in messages
                    bool hasResult = false;
                    for (auto rit = messages.rbegin(); rit != messages.rend() && rit->role == "tool"; ++rit)
                    {
                        if (rit->toolCallId == response.toolCalls[ti].id)
                        {
                            hasResult = true;
                            break;
                        }
                    }
                    if (!hasResult)
                    {
                        ContextBuilder::addToolResult(messages, response.toolCalls[ti].id,
                                                      response.toolCalls[ti].name,
                                                      "[tool call was not completed]");
                    }
                }
            }

            // check abort after tool execution
            if (activeTurnHandle && activeTurnHandle->aborted.load())
            {
                // flush synthetic error results for abandoned tool calls
                for (size_t ti = 0; ti < response.toolCalls.size(); ++ti)
                {
                    bool hasResult = false;
                    for (auto rit = messages.rbegin(); rit != messages.rend() && rit->role == "tool"; ++rit)
                    {
                        if (rit->toolCallId == response.toolCalls[ti].id)
                        {
                            hasResult = true;
                            break;
                        }
                    }
                    if (!hasResult)
                    {
                        ContextBuilder::addToolResult(messages, response.toolCalls[ti].id,
                                                      response.toolCalls[ti].name,
                                                      "[tool call was not completed]");
                    }
                }

                spdlog::info("[AgentLoop] Turn aborted by interrupt after tool execution");
                break;
            }
        }
        else
        {
            // no tool calls - final response
            if (!taskId.empty() && usageTracker.totalTokens > 0)
            {
                try
                {
                    taskManager->setUsage(taskId, usageTracker.toJson());
                }
                catch (...)
                {
                }
            }

            if (response.content.empty() && iteration > 0)
            {
                spdlog::warn("[AgentLoop] Empty response after tool execution at iteration {} (task {})", iteration, taskId);
            }

            return {response.content, blocks};
        }
    }

    // max iterations reached
    if (!taskId.empty() && usageTracker.totalTokens > 0)
    {
        try
        {
            taskManager->setUsage(taskId, usageTracker.toJson());
        }
        catch (...)
        {
        }
    }

    spdlog::warn("[AgentLoop] Agent loop hit max iterations ({})", maxIterations);
    return {"I've reached my processing limit. Please try again or simplify your request.", blocks};
}

StreamResult AgentLoop::consumeStream(
    const std::vector<ionclaw::provider::Message> &messages,
    const std::string &taskId,
    const std::string &chatId,
    const std::string &effectiveName,
    UsageTracker &usageTracker,
    AgentEventCallback &callback,
    const nlohmann::json &modelParams)
{
    StreamResult result;
    result.finishReason = "stop";

    std::vector<std::string> contentParts;
    std::vector<std::string> reasoningParts;

    // build request
    ionclaw::provider::ChatCompletionRequest request;
    request.messages = messages;
    request.model = agentConfig.model;
    auto baseTools = agentConfig.tools.empty() ? toolRegistry->getToolNames() : agentConfig.tools;
    auto effectiveTools = ionclaw::tool::ToolRegistry::applyToolPolicy(baseTools, agentConfig.toolPolicy);
    request.tools = toolRegistry->getOpenAiDefinitions(effectiveTools);
    request.stream = true;
    request.modelParams = modelParams;

    // process stream chunks as they arrive
    provider->chatStream(request, [&](const ionclaw::provider::StreamChunk &chunk)
                         {
        if (stopped.load())
        {
            return;
        }

        // accumulate content and broadcast to client
        if (chunk.type == "content" && !chunk.content.empty())
        {
            contentParts.push_back(chunk.content);

            AgentEvent event;
            event.type = "chat:stream";
            event.data = {
                {"task_id", taskId},
                {"chat_id", chatId},
                {"content", chunk.content},
                {"agent_name", effectiveName},
            };
            callback(event);
        }
        else if (chunk.type == "thinking" && !chunk.content.empty())
        {
            reasoningParts.push_back(chunk.content);
        }
        else if (chunk.type == "tool_call")
        {
            ionclaw::provider::ToolCall tc;
            tc.id = chunk.toolCall.id;
            tc.name = chunk.toolCall.name;
            tc.arguments = chunk.toolCall.arguments;
            result.toolCalls.push_back(tc);
        }
        else if (chunk.type == "usage" && !chunk.usage.is_null())
        {
            result.usage = chunk.usage;
            usageTracker.record(chunk.usage);
        }
        else if (chunk.type == "done")
        {
            if (!chunk.finishReason.empty())
            {
                result.finishReason = chunk.finishReason;
            }
        } });

    // assemble content
    std::ostringstream contentStream;

    for (const auto &part : contentParts)
    {
        contentStream << part;
    }

    // strip reasoning XML tags from content (some models emit <think>/<final> inline)
    result.content = ionclaw::util::StringHelper::stripReasoningTags(contentStream.str());

    // diagnostic: log when stream produced no content and no tool calls
    if (contentParts.empty() && result.toolCalls.empty())
    {
        spdlog::warn("[AgentLoop] consumeStream returned empty: no content, no tool calls (finishReason={})", result.finishReason);
    }

    // assemble reasoning
    std::ostringstream reasoningStream;

    for (const auto &part : reasoningParts)
    {
        reasoningStream << part;
    }

    result.reasoningContent = reasoningStream.str();

    // broadcast thinking if accumulated
    if (!result.reasoningContent.empty())
    {
        dispatcher->broadcast("chat:thinking", {
                                                   {"task_id", taskId},
                                                   {"chat_id", chatId},
                                                   {"content", result.reasoningContent},
                                                   {"agent_name", effectiveName},
                                               });
    }

    // signal end of stream (only if there was content and no tool calls)
    if (!contentParts.empty() && result.toolCalls.empty())
    {
        dispatcher->broadcast("chat:stream_end", {
                                                     {"task_id", taskId},
                                                     {"chat_id", chatId},
                                                     {"agent_name", effectiveName},
                                                 });
    }

    // determine finish reason if not "length"
    if (result.finishReason != "length")
    {
        result.finishReason = result.toolCalls.empty() ? "stop" : "tool_calls";
    }

    return result;
}

bool AgentLoop::tryMemoryFlush(
    std::vector<ionclaw::provider::Message> &messages,
    const ionclaw::tool::ToolContext &toolContext,
    const nlohmann::json &modelParams)
{
    if (agentConfig.workspace.empty())
    {
        return false;
    }

    static const std::string FLUSH_PROMPT =
        "Pre-compaction memory flush. "
        "Save any durable facts, decisions, or context from this conversation to the daily log "
        "(use memory_save tool). If nothing meaningful to save, reply with [SILENT].";

    // build flush messages: system prompt + current conversation + flush instruction
    auto flushMessages = messages;

    ionclaw::provider::Message userMsg;
    userMsg.role = "user";
    userMsg.content = FLUSH_PROMPT;
    flushMessages.push_back(userMsg);

    // use only memory_save tool
    auto flushRegistry = std::make_shared<ionclaw::tool::ToolRegistry>(false);
    flushRegistry->registerTool(std::make_shared<ionclaw::tool::builtin::MemorySaveTool>());

    auto tools = flushRegistry->getOpenAiDefinitions();

    try
    {
        auto response = provider->chat({
            flushMessages,
            agentConfig.model,
            0.7,
            2048,
            tools,
            false,
            modelParams,
        });

        if (!response.toolCalls.empty())
        {
            for (const auto &tc : response.toolCalls)
            {
                if (tc.name == "memory_save")
                {
                    nlohmann::json args = tc.arguments;

                    if (args.is_string())
                    {
                        try
                        {
                            args = nlohmann::json::parse(args.get<std::string>());
                        }
                        catch (...)
                        {
                        }
                    }

                    auto result = flushRegistry->executeTool(tc.name, args, toolContext);
                    spdlog::info("[AgentLoop] Pre-compaction memory flush: {}", result.text);
                }
            }

            return true;
        }
    }
    catch (const std::exception &e)
    {
        spdlog::warn("[AgentLoop] Pre-compaction memory flush failed: {}", e.what());
    }

    return false;
}

void AgentLoop::compactWithHooks(
    std::vector<ionclaw::provider::Message> &messages,
    const std::string &sessionKey,
    const std::string &taskId,
    const nlohmann::json &modelParams,
    const ionclaw::tool::ToolContext *toolContext)
{
    if (hookRunnerPtr)
    {
        HookContext ctx;
        ctx.agentName = agentName;
        ctx.sessionKey = sessionKey;
        ctx.taskId = taskId;
        hookRunnerPtr->run(HookPoint::BeforeCompaction, ctx);
    }

    // pre-compaction memory flush: save durable facts before summarizing (once per compaction)
    compactionCount++;

    if (toolContext && memoryFlushCompactionCount < compactionCount)
    {
        if (tryMemoryFlush(messages, *toolContext, modelParams))
        {
            memoryFlushCompactionCount = compactionCount;
        }
    }

    messages = Compaction::compact(messages, provider, agentConfig.model, modelParams);

    if (hookRunnerPtr)
    {
        HookContext ctx;
        ctx.agentName = agentName;
        ctx.sessionKey = sessionKey;
        ctx.taskId = taskId;
        hookRunnerPtr->run(HookPoint::AfterCompaction, ctx);
    }
}

void AgentLoop::stop()
{
    stopped.store(true);
    spdlog::info("[AgentLoop] Agent '{}' stopped", agentName);
}

} // namespace agent
} // namespace ionclaw
