#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "nlohmann/json.hpp"

#include "ionclaw/bus/EventDispatcher.hpp"
#include "ionclaw/bus/Events.hpp"
#include "ionclaw/bus/MessageBus.hpp"
#include "ionclaw/session/SessionManager.hpp"
#include "ionclaw/task/TaskManager.hpp"

namespace ionclaw
{
namespace channel
{

class TelegramRunner
{
public:
    TelegramRunner(
        std::shared_ptr<ionclaw::bus::MessageBus> bus,
        std::shared_ptr<ionclaw::session::SessionManager> sessionManager,
        std::shared_ptr<ionclaw::task::TaskManager> taskManager,
        std::shared_ptr<ionclaw::bus::EventDispatcher> dispatcher,
        std::string token,
        std::vector<std::string> allowedUsers,
        std::string proxy,
        bool replyToMessage,
        std::string publicDir);

    void start();
    void stop();

private:
    void pollLoop();
    void outboundLoop();
    bool isAllowed(const std::string &userId, const std::string &username) const;
    void processUpdate(const nlohmann::json &update);

    // telegram api helpers
    static const char *TELEGRAM_API;
    static constexpr int POLL_TIMEOUT_SEC = 30;
    static constexpr int OUTBOUND_POLL_MS = 500;
    static constexpr size_t MAX_MESSAGE_LENGTH = 4000;

    static std::string telegramGet(const std::string &token, const std::string &path, const std::string &proxy, int timeoutSeconds = 30);
    static std::string telegramPost(const std::string &token, const std::string &path, const std::string &body, const std::string &proxy);

    // media helpers
    std::string downloadTelegramFile(const std::string &fileId);
    std::string mediaDatePath() const;

    // outbound helpers
    void sendTypingAction(const std::string &chatId);
    void sendTextMessage(const std::string &chatId, const std::string &text, int replyToMessageId = 0);
    void sendChunkedMessage(const std::string &chatId, const std::string &text, int replyToMessageId = 0);

    // markdown to telegram html
    static std::string markdownToTelegramHtml(const std::string &md);

    std::shared_ptr<ionclaw::bus::MessageBus> bus;
    std::shared_ptr<ionclaw::session::SessionManager> sessionManager;
    std::shared_ptr<ionclaw::task::TaskManager> taskManager;
    std::shared_ptr<ionclaw::bus::EventDispatcher> dispatcher;
    std::string token;
    std::vector<std::string> allowedUsers;
    std::string proxy;
    bool replyToMessage;
    std::string publicDir;

    std::atomic<bool> running{false};
    std::thread pollThread;
    std::thread outboundThread;
    int64_t lastUpdateId{0};
};

} // namespace channel
} // namespace ionclaw
