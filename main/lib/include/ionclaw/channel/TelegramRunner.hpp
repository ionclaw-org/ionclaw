#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
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
    TelegramRunner(std::shared_ptr<ionclaw::bus::MessageBus> bus, std::shared_ptr<ionclaw::session::SessionManager> sessionManager, std::shared_ptr<ionclaw::task::TaskManager> taskManager, std::shared_ptr<ionclaw::bus::EventDispatcher> dispatcher, std::string token, std::vector<std::string> allowedUsers, std::string proxy, bool replyToMessage, std::string publicDir, std::string publicUrl);

    ~TelegramRunner();

    void start();
    void stop();

private:
    void pollLoop();
    void outboundLoop();
    bool isAllowed(const std::string &userId, const std::string &username) const;
    void processUpdate(const nlohmann::json &update);

    static const char *TELEGRAM_API;
    static constexpr int POLL_TIMEOUT_SEC = 30;
    static constexpr int OUTBOUND_POLL_MS = 500;
    static constexpr size_t MAX_MESSAGE_LENGTH = 4000;

    static std::string telegramGet(const std::string &token, const std::string &path, const std::string &proxy, int timeoutSeconds = 30);
    static std::string telegramPost(const std::string &token, const std::string &path, const std::string &body, const std::string &proxy);

    std::string downloadTelegramFile(const std::string &fileId);
    std::string mediaDatePath() const;

    void sendTypingAction(const std::string &chatId);
    void sendTextMessage(const std::string &chatId, const std::string &text, int replyToMessageId = 0);
    void sendChunkedMessage(const std::string &chatId, const std::string &text, int replyToMessageId = 0);

    // sends a media attachment by public url; kind is image|audio|video|document
    void sendMediaByUrl(const std::string &chatId, const std::string &kind, const std::string &url, const std::string &caption, int replyToMessageId = 0);

    // turns a workspace media path into its public url, passing absolute urls through unchanged
    std::string resolvePublicUrl(const std::string &path) const;

    static std::string markdownToTelegramHtml(const std::string &md);
    static std::string escapeHtml(const std::string &s);
    static size_t findClosing(const std::string &s, size_t pos, const std::string &marker);

    std::shared_ptr<ionclaw::bus::MessageBus> bus;
    std::shared_ptr<ionclaw::session::SessionManager> sessionManager;
    std::shared_ptr<ionclaw::task::TaskManager> taskManager;
    std::shared_ptr<ionclaw::bus::EventDispatcher> dispatcher;
    std::string token;
    std::vector<std::string> allowedUsers;
    std::string proxy;
    bool replyToMessage;
    std::string publicDir;
    std::string publicUrl;

    std::atomic<bool> running{false};
    std::thread pollThread;
    std::thread outboundThread;
    int64_t lastUpdateId{0};

    static constexpr int TYPING_INTERVAL_SEC = 4;
    void startTypingTicker(const std::string &chatId);
    void stopTypingTicker(const std::string &chatId);
    void stopAllTypingTickers();
    void registerTypingHandler();
    void unregisterTypingHandler();

    std::mutex typingMutex;
    std::condition_variable typingCv;
    std::unordered_map<std::string, std::thread> typingTickers;
    std::unordered_map<std::string, bool> typingActive;
};

} // namespace channel
} // namespace ionclaw
