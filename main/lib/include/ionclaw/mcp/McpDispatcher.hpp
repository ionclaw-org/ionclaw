#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "ionclaw/config/Config.hpp"
#include "ionclaw/mcp/McpTypes.hpp"

namespace ionclaw
{
namespace agent
{
class Orchestrator;
}
namespace bus
{
class EventDispatcher;
class MessageBus;
} // namespace bus
namespace session
{
class SessionManager;
}
namespace task
{
class TaskManager;
}
} // namespace ionclaw

namespace ionclaw
{
namespace mcp
{

using SseCallback = std::function<bool(const nlohmann::json &event)>;

class McpDispatcher
{
public:
    McpDispatcher(
        std::shared_ptr<ionclaw::agent::Orchestrator> orchestrator,
        std::shared_ptr<ionclaw::session::SessionManager> sessionManager,
        std::shared_ptr<ionclaw::task::TaskManager> taskManager,
        std::shared_ptr<ionclaw::bus::MessageBus> bus,
        std::shared_ptr<ionclaw::bus::EventDispatcher> dispatcher,
        std::shared_ptr<ionclaw::config::Config> config);

    void enable();
    void disable();
    bool isEnabled() const;
    bool requiresAuth() const;
    bool verifyToken(const std::string &token) const;
    bool isAllowedOrigin(const std::string &origin) const;

    std::string createSession();
    bool hasSession(const std::string &id) const;
    void closeSession(const std::string &id);

    // dispatch a JSON-RPC request; sseCallback is non-null for streaming responses
    nlohmann::json dispatch(
        const std::string &sessionId,
        const JsonRpcRequest &request,
        SseCallback *sseCallback);

private:
    nlohmann::json handleInitialize(const std::string &sessionId, const JsonRpcRequest &req);
    nlohmann::json handleInitialized(const std::string &sessionId, const JsonRpcRequest &req);
    nlohmann::json handlePing(const JsonRpcRequest &req);
    nlohmann::json handleToolsList(const JsonRpcRequest &req);
    nlohmann::json handleToolsCall(const std::string &sessionId, const JsonRpcRequest &req, SseCallback *sseCallback);
    nlohmann::json handleResourcesList(const JsonRpcRequest &req);
    nlohmann::json handleResourcesTemplatesList(const JsonRpcRequest &req);
    nlohmann::json handleResourcesRead(const JsonRpcRequest &req);

    nlohmann::json toolChat(const std::string &sessionId, const nlohmann::json &args, nlohmann::json progressToken, SseCallback *sseCallback);
    nlohmann::json toolAbort(const std::string &sessionId, const nlohmann::json &args);
    nlohmann::json toolListSessions(const nlohmann::json &args);
    nlohmann::json toolGetSession(const nlohmann::json &args);
    nlohmann::json toolDeleteSession(const nlohmann::json &args);
    nlohmann::json toolListAgents(const nlohmann::json &args);
    nlohmann::json toolListTasks(const nlohmann::json &args);
    nlohmann::json toolGetTask(const nlohmann::json &args);

    nlohmann::json resourceSessions();
    nlohmann::json resourceSession(const std::string &chatId);
    nlohmann::json resourceAgents();

    static nlohmann::json toolSchema(
        const std::string &name,
        const std::string &description,
        const nlohmann::json &properties,
        const std::vector<std::string> &required);

    std::shared_ptr<ionclaw::agent::Orchestrator> orchestrator;
    std::shared_ptr<ionclaw::session::SessionManager> sessionManager;
    std::shared_ptr<ionclaw::task::TaskManager> taskManager;
    std::shared_ptr<ionclaw::bus::MessageBus> bus;
    std::shared_ptr<ionclaw::bus::EventDispatcher> dispatcher;
    std::shared_ptr<ionclaw::config::Config> config;

    // starts disabled; ChannelManager::startMcp() calls enable()
    std::atomic<bool> enabled{false};
    mutable std::mutex sessionsMutex;
    std::map<std::string, McpSession> sessions;
};

} // namespace mcp
} // namespace ionclaw
