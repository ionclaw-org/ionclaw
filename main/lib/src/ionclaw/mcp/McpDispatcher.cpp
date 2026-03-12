#include "ionclaw/mcp/McpDispatcher.hpp"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <stdexcept>

#include "spdlog/spdlog.h"

#include "ionclaw/agent/Orchestrator.hpp"
#include "ionclaw/bus/EventDispatcher.hpp"
#include "ionclaw/bus/Events.hpp"
#include "ionclaw/bus/MessageBus.hpp"
#include "ionclaw/session/SessionManager.hpp"
#include "ionclaw/task/TaskManager.hpp"
#include "ionclaw/util/StringHelper.hpp"
#include "ionclaw/util/TimeHelper.hpp"
#include "ionclaw/util/UniqueId.hpp"

namespace ionclaw
{
namespace mcp
{

namespace
{

static constexpr int CHAT_TIMEOUT_SECONDS = 300;

// shared state between EventDispatcher callback thread and HTTP handler thread
struct ChatStream
{
    std::mutex mtx;
    std::condition_variable cv;
    std::deque<std::string> chunks;
    bool done = false;
    bool error = false;
    std::string errorMsg;
    std::string fullText;
};

} // namespace

McpDispatcher::McpDispatcher(
    std::shared_ptr<ionclaw::agent::Orchestrator> orchestrator,
    std::shared_ptr<ionclaw::session::SessionManager> sessionManager,
    std::shared_ptr<ionclaw::task::TaskManager> taskManager,
    std::shared_ptr<ionclaw::bus::MessageBus> bus,
    std::shared_ptr<ionclaw::bus::EventDispatcher> dispatcher,
    std::shared_ptr<ionclaw::config::Config> config)
    : orchestrator(std::move(orchestrator))
    , sessionManager(std::move(sessionManager))
    , taskManager(std::move(taskManager))
    , bus(std::move(bus))
    , dispatcher(std::move(dispatcher))
    , config(std::move(config))
{
}

void McpDispatcher::enable()
{
    enabled.store(true);
    spdlog::info("[MCP] Channel enabled");
}

void McpDispatcher::disable()
{
    enabled.store(false);

    std::lock_guard<std::mutex> lock(sessionsMutex);
    auto count = sessions.size();
    sessions.clear();

    spdlog::info("[MCP] Channel disabled ({} sessions cleared)", count);
}

bool McpDispatcher::isEnabled() const
{
    return enabled.load();
}

bool McpDispatcher::requiresAuth() const
{
    auto it = config->channels.find(MCP_CHANNEL);
    if (it == config->channels.end())
    {
        return false;
    }
    const auto &ch = it->second;
    if (ch.raw.contains("require_auth") && ch.raw["require_auth"].is_boolean())
    {
        return ch.raw["require_auth"].get<bool>();
    }
    return false;
}

bool McpDispatcher::verifyToken(const std::string &token) const
{
    if (token.empty())
    {
        return false;
    }

    auto chanIt = config->channels.find(MCP_CHANNEL);
    if (chanIt == config->channels.end())
    {
        return false;
    }

    const auto &credName = chanIt->second.credential;
    if (!credName.empty())
    {
        auto credIt = config->credentials.find(credName);
        if (credIt != config->credentials.end() && !credIt->second.key.empty())
        {
            return token == credIt->second.key;
        }
    }

    return false;
}

std::string McpDispatcher::createSession()
{
    auto id = ionclaw::util::UniqueId::uuid();
    McpSession session;
    session.id = id;
    session.chatId = id;
    std::lock_guard<std::mutex> lock(sessionsMutex);
    sessions[id] = std::move(session);
    return id;
}

bool McpDispatcher::hasSession(const std::string &id) const
{
    std::lock_guard<std::mutex> lock(sessionsMutex);
    return sessions.find(id) != sessions.end();
}

void McpDispatcher::closeSession(const std::string &id)
{
    std::lock_guard<std::mutex> lock(sessionsMutex);
    sessions.erase(id);
    spdlog::info("[MCP] Session closed: {}", id);
}

nlohmann::json McpDispatcher::dispatch(
    const std::string &sessionId,
    const JsonRpcRequest &request,
    SseCallback *sseCallback)
{
    if (request.jsonrpc != "2.0")
    {
        return JsonRpcResponse::err(request.id, static_cast<int>(RpcErrorCode::InvalidRequest), "Invalid jsonrpc version");
    }

    const auto &method = request.method;

    if (method == "initialize")
    {
        return handleInitialize(sessionId, request);
    }

    // all methods past initialize require a valid session
    {
        std::lock_guard<std::mutex> lock(sessionsMutex);
        auto it = sessions.find(sessionId);
        if (it == sessions.end())
        {
            spdlog::warn("[MCP] Session not found for method '{}': {}", method, sessionId);
            return JsonRpcResponse::err(request.id, static_cast<int>(RpcErrorCode::InvalidRequest), "Session not found");
        }
        if (method != "notifications/initialized" && !it->second.initialized)
        {
            spdlog::warn("[MCP] Session not initialized for method '{}': {}", method, sessionId);
            return JsonRpcResponse::err(request.id, static_cast<int>(RpcErrorCode::InvalidRequest), "Session not initialized");
        }
    }

    if (method == "notifications/initialized")
        return handleInitialized(sessionId, request);
    if (method == "ping")
        return handlePing(request);
    if (method == "tools/list")
        return handleToolsList(request);
    if (method == "tools/call")
        return handleToolsCall(sessionId, request, sseCallback);
    if (method == "resources/list")
        return handleResourcesList(request);
    if (method == "resources/templates/list")
        return handleResourcesTemplatesList(request);
    if (method == "resources/read")
        return handleResourcesRead(request);
    if (method == "resources/subscribe" || method == "resources/unsubscribe")
        return JsonRpcResponse::ok(request.id, nlohmann::json::object());
    return JsonRpcResponse::err(request.id, static_cast<int>(RpcErrorCode::MethodNotFound), "Method not found: " + method);
}

nlohmann::json McpDispatcher::handleInitialize(const std::string &sessionId, const JsonRpcRequest &req)
{
    auto clientVersion = req.params.value("protocolVersion", std::string(""));

    // accept the client version if supported, otherwise fall back to ours
    // supports: 2024-11-05, 2025-03-26, 2025-11-25
    std::string negotiatedVersion = MCP_PROTOCOL_VERSION;
    if (clientVersion == "2024-11-05" || clientVersion == "2025-03-26" || clientVersion == "2025-11-25")
    {
        negotiatedVersion = clientVersion;
    }

    {
        std::lock_guard<std::mutex> lock(sessionsMutex);
        auto it = sessions.find(sessionId);
        if (it != sessions.end())
        {
            it->second.protocolVersion = negotiatedVersion;
        }
    }

    spdlog::info("[MCP] Session initialized: {} (protocol: {})", sessionId, negotiatedVersion);

    return JsonRpcResponse::ok(req.id, {{"protocolVersion", negotiatedVersion},
                                        {"serverInfo", {{"name", "IonClaw"}, {"version", IONCLAW_VERSION_STRING}}},
                                        {"capabilities", {{"tools", {{"listChanged", false}}}, {"resources", {{"listChanged", false}, {"subscribe", false}}}}}});
}

nlohmann::json McpDispatcher::handleInitialized(const std::string &sessionId, const JsonRpcRequest &)
{
    {
        std::lock_guard<std::mutex> lock(sessionsMutex);
        auto it = sessions.find(sessionId);
        if (it != sessions.end())
        {
            it->second.initialized = true;
        }
    }
    spdlog::info("[MCP] Session ready: {}", sessionId);
    return nlohmann::json(nullptr);
}

nlohmann::json McpDispatcher::handlePing(const JsonRpcRequest &req)
{
    return JsonRpcResponse::ok(req.id, nlohmann::json::object());
}

nlohmann::json McpDispatcher::handleToolsList(const JsonRpcRequest &req)
{
    auto tools = nlohmann::json::array();

    tools.push_back(toolSchema("chat",
                               "Send a message to the AI agent and receive a response",
                               {{"message", {{"type", "string"}, {"description", "The message to send"}}},
                                {"session_id", {{"type", "string"}, {"description", "Chat session ID (optional, uses MCP session if omitted)"}}},
                                {"agent", {{"type", "string"}, {"description", "Target agent name (optional, uses default routing)"}}}},
                               {"message"}));

    tools.push_back(toolSchema("abort",
                               "Abort an active agent session",
                               {{"session_id", {{"type", "string"}, {"description", "Chat session ID to abort"}}}},
                               {"session_id"}));

    tools.push_back(toolSchema("list_sessions",
                               "List all chat sessions",
                               nlohmann::json::object(),
                               {}));

    tools.push_back(toolSchema("get_session",
                               "Get session details and message history",
                               {{"session_id", {{"type", "string"}, {"description", "Session key (e.g. web:uuid or mcp:uuid)"}}}},
                               {"session_id"}));

    tools.push_back(toolSchema("delete_session",
                               "Delete a chat session and its history",
                               {{"session_id", {{"type", "string"}, {"description", "Session key to delete"}}}},
                               {"session_id"}));

    tools.push_back(toolSchema("list_agents",
                               "List all configured agents with their descriptions and models",
                               nlohmann::json::object(),
                               {}));

    tools.push_back(toolSchema("list_tasks",
                               "List recent tasks",
                               {{"limit", {{"type", "integer"}, {"description", "Maximum number of tasks to return (default: 50)"}}}},
                               {}));

    tools.push_back(toolSchema("get_task",
                               "Get task details by ID",
                               {{"task_id", {{"type", "string"}, {"description", "Task ID"}}}},
                               {"task_id"}));

    return JsonRpcResponse::ok(req.id, {{"tools", tools}});
}

nlohmann::json McpDispatcher::handleToolsCall(
    const std::string &sessionId,
    const JsonRpcRequest &req,
    SseCallback *sseCallback)
{
    auto name = req.params.value("name", std::string(""));
    auto args = req.params.value("arguments", nlohmann::json::object());

    // extract client-provided progress token for spec-compliant notifications/progress
    // MCP spec: progressToken can be string | number — preserve original type
    nlohmann::json progressToken;
    if (req.params.contains("_meta") && req.params["_meta"].is_object())
    {
        auto &meta = req.params["_meta"];
        if (meta.contains("progressToken"))
        {
            progressToken = meta["progressToken"];
        }
    }

    try
    {
        nlohmann::json result;

        if (name == "chat")
            result = toolChat(sessionId, args, std::move(progressToken), sseCallback);
        else if (name == "abort")
            result = toolAbort(sessionId, args);
        else if (name == "list_sessions")
            result = toolListSessions(args);
        else if (name == "get_session")
            result = toolGetSession(args);
        else if (name == "delete_session")
            result = toolDeleteSession(args);
        else if (name == "list_agents")
            result = toolListAgents(args);
        else if (name == "list_tasks")
            result = toolListTasks(args);
        else if (name == "get_task")
            result = toolGetTask(args);
        else
        {
            return JsonRpcResponse::err(req.id, static_cast<int>(RpcErrorCode::MethodNotFound), "Unknown tool: " + name);
        }

        std::string text = result.is_string() ? result.get<std::string>() : result.dump(2);

        return JsonRpcResponse::ok(req.id, {{"content", nlohmann::json::array({{{"type", "text"}, {"text", text}}})},
                                            {"isError", false}});
    }
    catch (const std::exception &e)
    {
        return JsonRpcResponse::ok(req.id, {{"content", nlohmann::json::array({{{"type", "text"}, {"text", std::string(e.what())}}})},
                                            {"isError", true}});
    }
}

nlohmann::json McpDispatcher::toolChat(
    const std::string &sessionId,
    const nlohmann::json &args,
    nlohmann::json progressToken,
    SseCallback *sseCallback)
{
    auto message = args.value("message", std::string(""));
    if (message.empty())
    {
        throw std::runtime_error("message is required");
    }
    auto agentOverride = args.value("agent", std::string(""));

    // resolve the chatId from the MCP session
    std::string chatId;
    {
        std::lock_guard<std::mutex> lock(sessionsMutex);
        auto it = sessions.find(sessionId);
        if (it == sessions.end())
        {
            throw std::runtime_error("Session not found");
        }
        chatId = it->second.chatId;
    }

    // allow caller to target a specific existing chat session
    auto requestedSessionId = args.value("session_id", std::string(""));
    if (!requestedSessionId.empty())
    {
        auto colonPos = requestedSessionId.find(':');
        chatId = (colonPos != std::string::npos)
                     ? requestedSessionId.substr(colonPos + 1)
                     : requestedSessionId;
    }

    auto sessionKey = std::string(MCP_CHANNEL) + ":" + chatId;

    spdlog::info("[MCP] chat tool called (session: {}, streaming: {})", sessionKey, sseCallback ? "yes" : "no");

    // create task for tracking
    auto taskTitle = ionclaw::util::StringHelper::utf8SafeTruncate(message, 100);
    auto task = taskManager->createTask(taskTitle, message, MCP_CHANNEL, chatId);
    auto taskId = task.id;

    // persist user message so history is available immediately
    sessionManager->ensureSession(sessionKey);
    ionclaw::session::SessionMessage userMsg;
    userMsg.role = "user";
    userMsg.content = message;
    userMsg.timestamp = ionclaw::util::TimeHelper::now();
    sessionManager->addMessage(sessionKey, userMsg);
    dispatcher->broadcast("sessions:updated", nlohmann::json::object());

    // subscribe to agent output events for this task/session
    auto stream = std::make_shared<ChatStream>();
    auto handlerId = "mcp-" + ionclaw::util::UniqueId::shortId();

    dispatcher->addNamedHandler(handlerId,
                                [stream, sessionKey, taskId](const std::string &eventType, const nlohmann::json &data)
                                {
                                    if (eventType == "chat:stream")
                                    {
                                        if (data.value("chat_id", "") == sessionKey)
                                        {
                                            auto chunk = data.value("content", std::string(""));
                                            if (!chunk.empty())
                                            {
                                                std::lock_guard<std::mutex> lock(stream->mtx);
                                                stream->chunks.push_back(chunk);
                                                stream->fullText += chunk;
                                                stream->cv.notify_all();
                                            }
                                        }
                                    }
                                    else if (eventType == "task:updated")
                                    {
                                        if (data.value("id", "") == taskId)
                                        {
                                            auto state = data.value("state", std::string(""));
                                            bool isDoneState = (state == "done" || state == "DONE");
                                            bool isErrorState = (state == "error" || state == "ERROR");
                                            if (isDoneState || isErrorState)
                                            {
                                                std::lock_guard<std::mutex> lock(stream->mtx);
                                                stream->done = true;
                                                if (isErrorState)
                                                {
                                                    stream->error = true;
                                                    stream->errorMsg = data.value("result", std::string("Task failed"));
                                                }
                                                else if (stream->fullText.empty())
                                                {
                                                    stream->fullText = data.value("result", std::string(""));
                                                }
                                                stream->cv.notify_all();
                                            }
                                        }
                                    }
                                });

    // RAII guard: ensure handler is always removed, even on exceptions
    auto handlerGuard = std::shared_ptr<void>(nullptr, [this, &handlerId](void *)
                                              { dispatcher->removeHandler(handlerId); });

    // use client-provided progress token if available, otherwise fall back to taskId
    const auto effectiveProgressToken = progressToken.is_null() ? nlohmann::json(taskId) : std::move(progressToken);

    // publish message to the agent
    ionclaw::bus::InboundMessage inbound;
    inbound.channel = MCP_CHANNEL;
    inbound.senderId = "mcp-client";
    inbound.chatId = chatId;
    inbound.content = message;
    inbound.metadata = {{"task_id", taskId}, {"message_saved", true}};
    if (!agentOverride.empty())
    {
        inbound.metadata["agent_override"] = agentOverride;
    }
    spdlog::info("[MCP] Publishing message to bus (session: {}, task: {})", sessionKey, taskId);
    bus->publishInbound(inbound);

    if (sseCallback)
    {
        // streaming mode: forward chunks as notifications/progress events
        static constexpr int POLL_INTERVAL_SECONDS = 30;
        int elapsed = 0;

        while (true)
        {
            std::string chunk;
            bool isDone = false;
            bool isError = false;
            std::string errorMsg;

            {
                std::unique_lock<std::mutex> lock(stream->mtx);
                stream->cv.wait_for(lock, std::chrono::seconds(POLL_INTERVAL_SECONDS),
                                    [&stream]
                                    { return !stream->chunks.empty() || stream->done; });

                if (!stream->chunks.empty())
                {
                    chunk = stream->chunks.front();
                    stream->chunks.pop_front();
                }
                isDone = stream->done && stream->chunks.empty();
                isError = stream->error;
                errorMsg = stream->errorMsg;
            }

            if (!chunk.empty())
            {
                elapsed = 0;
                auto event = JsonRpcResponse::notification("notifications/progress", {{"progressToken", effectiveProgressToken},
                                                                                      {"message", chunk}});
                if (!(*sseCallback)(event))
                {
                    return nlohmann::json(stream->fullText);
                }
            }
            else
            {
                elapsed += POLL_INTERVAL_SECONDS;
            }

            if (isDone)
            {
                if (isError && stream->fullText.empty())
                {
                    throw std::runtime_error(errorMsg.empty() ? "Agent returned error" : errorMsg);
                }
                return nlohmann::json(stream->fullText);
            }

            if (elapsed >= CHAT_TIMEOUT_SECONDS)
            {
                throw std::runtime_error("Chat timed out after " + std::to_string(CHAT_TIMEOUT_SECONDS) + " seconds");
            }
        }
    }
    else
    {
        // non-streaming: wait for task completion
        std::unique_lock<std::mutex> lock(stream->mtx);
        bool finished = stream->cv.wait_for(lock, std::chrono::seconds(CHAT_TIMEOUT_SECONDS),
                                            [&stream]
                                            { return stream->done; });

        if (!finished)
        {
            throw std::runtime_error("Chat timed out after " + std::to_string(CHAT_TIMEOUT_SECONDS) + " seconds");
        }
        if (stream->error && stream->fullText.empty())
        {
            throw std::runtime_error(stream->errorMsg.empty() ? "Agent returned error" : stream->errorMsg);
        }
        return nlohmann::json(stream->fullText);
    }
}

nlohmann::json McpDispatcher::toolAbort(const std::string &, const nlohmann::json &args)
{
    auto rawId = args.value("session_id", std::string(""));
    if (rawId.empty())
    {
        throw std::runtime_error("session_id is required");
    }

    // normalize to full session key
    auto sessionKey = rawId;
    if (rawId.find(':') == std::string::npos)
    {
        sessionKey = std::string(MCP_CHANNEL) + ":" + rawId;
    }

    auto turn = orchestrator->getActiveTurn(sessionKey);
    if (!turn)
    {
        return {{"status", "no_active_turn"}, {"session_id", sessionKey}};
    }

    turn->aborted.store(true);
    return {{"status", "aborted"}, {"session_id", sessionKey}};
}

nlohmann::json McpDispatcher::toolListSessions(const nlohmann::json &)
{
    auto result = resourceSessions();
    result["count"] = static_cast<int>(result["sessions"].size());
    return result;
}

nlohmann::json McpDispatcher::toolGetSession(const nlohmann::json &args)
{
    auto rawId = args.value("session_id", std::string(""));
    if (rawId.empty())
    {
        throw std::runtime_error("session_id is required");
    }

    // find session metadata without creating a ghost session
    auto infos = sessionManager->listSessions();
    std::string createdAt, updatedAt;
    bool found = false;

    for (const auto &info : infos)
    {
        if (info.key == rawId)
        {
            createdAt = info.createdAt;
            updatedAt = info.updatedAt;
            found = true;
            break;
        }
    }

    if (!found)
    {
        throw std::runtime_error("Session not found: " + rawId);
    }

    auto messages = sessionManager->getHistory(rawId);
    auto msgArray = nlohmann::json::array();
    for (const auto &msg : messages)
    {
        msgArray.push_back(msg.toJson());
    }

    return {
        {"key", rawId},
        {"messages", msgArray},
        {"created_at", createdAt},
        {"updated_at", updatedAt}};
}

nlohmann::json McpDispatcher::toolDeleteSession(const nlohmann::json &args)
{
    auto rawId = args.value("session_id", std::string(""));
    if (rawId.empty())
    {
        throw std::runtime_error("session_id is required");
    }

    sessionManager->deleteSession(rawId);
    dispatcher->broadcast("sessions:updated", nlohmann::json::object());
    return {{"status", "deleted"}, {"session_id", rawId}};
}

nlohmann::json McpDispatcher::toolListAgents(const nlohmann::json &)
{
    return resourceAgents();
}

nlohmann::json McpDispatcher::toolListTasks(const nlohmann::json &args)
{
    auto limit = std::max(1, args.value("limit", 50));
    auto all = taskManager->listTasks();
    auto result = nlohmann::json::array();

    auto start = (static_cast<int>(all.size()) > limit)
                     ? all.size() - static_cast<size_t>(limit)
                     : 0;

    for (size_t i = start; i < all.size(); ++i)
    {
        result.push_back(all[i].toJson());
    }

    return {{"tasks", result}, {"count", static_cast<int>(result.size())}};
}

nlohmann::json McpDispatcher::toolGetTask(const nlohmann::json &args)
{
    auto taskId = args.value("task_id", std::string(""));
    if (taskId.empty())
    {
        throw std::runtime_error("task_id is required");
    }

    auto task = taskManager->getTask(taskId);
    return task.toJson();
}

nlohmann::json McpDispatcher::handleResourcesList(const JsonRpcRequest &req)
{
    return JsonRpcResponse::ok(req.id, {{"resources", nlohmann::json::array({{{"uri", "ionclaw://sessions"},
                                                                              {"name", "Sessions"},
                                                                              {"mimeType", "application/json"}},
                                                                             {{"uri", "ionclaw://agents"},
                                                                              {"name", "Agents"},
                                                                              {"mimeType", "application/json"}}})}});
}

nlohmann::json McpDispatcher::handleResourcesTemplatesList(const JsonRpcRequest &req)
{
    return JsonRpcResponse::ok(req.id, {{"resourceTemplates", nlohmann::json::array({{{"uriTemplate", "ionclaw://sessions/{id}"},
                                                                                      {"name", "Session by ID"},
                                                                                      {"mimeType", "application/json"}}})}});
}

nlohmann::json McpDispatcher::handleResourcesRead(const JsonRpcRequest &req)
{
    auto uri = req.params.value("uri", std::string(""));

    if (uri == "ionclaw://sessions")
    {
        return JsonRpcResponse::ok(req.id, {{"contents", nlohmann::json::array({{{"uri", uri},
                                                                                 {"mimeType", "application/json"},
                                                                                 {"text", resourceSessions().dump(2)}}})}});
    }

    if (uri == "ionclaw://agents")
    {
        return JsonRpcResponse::ok(req.id, {{"contents", nlohmann::json::array({{{"uri", uri},
                                                                                 {"mimeType", "application/json"},
                                                                                 {"text", resourceAgents().dump(2)}}})}});
    }

    // ionclaw://sessions/{id} template
    static const std::string SESSION_PREFIX = "ionclaw://sessions/";
    if (uri.substr(0, SESSION_PREFIX.size()) == SESSION_PREFIX)
    {
        auto chatId = uri.substr(SESSION_PREFIX.size());
        return JsonRpcResponse::ok(req.id, {{"contents", nlohmann::json::array({{{"uri", uri},
                                                                                 {"mimeType", "application/json"},
                                                                                 {"text", resourceSession(chatId).dump(2)}}})}});
    }

    return JsonRpcResponse::err(req.id, static_cast<int>(RpcErrorCode::InvalidParams), "Unknown resource URI: " + uri);
}

nlohmann::json McpDispatcher::resourceSessions()
{
    auto infos = sessionManager->listSessions();
    auto result = nlohmann::json::array();

    for (const auto &info : infos)
    {
        result.push_back({{"key", info.key},
                          {"channel", info.channel},
                          {"display_name", info.displayName},
                          {"created_at", info.createdAt},
                          {"updated_at", info.updatedAt}});
    }

    return {{"sessions", result}};
}

nlohmann::json McpDispatcher::resourceSession(const std::string &chatId)
{
    auto infos = sessionManager->listSessions();

    std::string sessionKey, createdAt, updatedAt;
    for (const auto &info : infos)
    {
        auto colonPos = info.key.find(':');
        if (colonPos != std::string::npos && info.key.substr(colonPos + 1) == chatId)
        {
            sessionKey = info.key;
            createdAt = info.createdAt;
            updatedAt = info.updatedAt;
            break;
        }
    }

    if (sessionKey.empty())
    {
        throw std::runtime_error("Session not found for id: " + chatId);
    }

    auto messages = sessionManager->getHistory(sessionKey);
    auto msgArray = nlohmann::json::array();
    for (const auto &msg : messages)
    {
        msgArray.push_back(msg.toJson());
    }

    return {
        {"key", sessionKey},
        {"messages", msgArray},
        {"created_at", createdAt},
        {"updated_at", updatedAt}};
}

nlohmann::json McpDispatcher::resourceAgents()
{
    auto result = nlohmann::json::array();

    for (const auto &[name, agent] : config->agents)
    {
        result.push_back({{"name", name},
                          {"description", agent.description},
                          {"model", agent.model}});
    }

    return {{"agents", result}};
}

nlohmann::json McpDispatcher::toolSchema(
    const std::string &name,
    const std::string &description,
    const nlohmann::json &properties,
    const std::vector<std::string> &required)
{
    auto schema = nlohmann::json{
        {"type", "object"},
        {"properties", properties}};

    if (!required.empty())
    {
        schema["required"] = required;
    }

    return {
        {"name", name},
        {"description", description},
        {"inputSchema", schema}};
}

} // namespace mcp
} // namespace ionclaw
