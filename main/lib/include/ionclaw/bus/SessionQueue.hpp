#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "ionclaw/bus/Events.hpp"
#include "ionclaw/config/Config.hpp"

namespace ionclaw
{
namespace bus
{

struct QueuedItem
{
    InboundMessage message;
    QueueMode mode;
    std::chrono::steady_clock::time_point enqueuedAt;
};

struct QueueSettings
{
    QueueMode mode = QueueMode::Collect;
    int debounceMs = 1000;
    int cap = 20;
    QueueDropPolicy dropPolicy = QueueDropPolicy::Summarize;
};

class SessionQueue
{
public:
    static constexpr int DEFAULT_DEBOUNCE_MS = 1000;
    static constexpr int DEFAULT_CAP = 20;

    static std::string queueModeToString(QueueMode mode);
    static std::optional<QueueMode> normalizeQueueMode(const std::string &raw);
    static std::optional<QueueDropPolicy> normalizeQueueDropPolicy(const std::string &raw);
    static QueueSettings resolveQueueSettings(const config::Config &config, const std::string &channel, std::optional<QueueMode> inlineMode = std::nullopt);

    bool enqueue(const std::string &sessionKey, const InboundMessage &msg, QueueMode mode, const QueueSettings &settings);

    std::vector<QueuedItem> drainSteer(const std::string &sessionKey);
    std::vector<QueuedItem> drainFollowup(const std::string &sessionKey);

    // drops the followup backup twin of a steer_backlog message once its steer copy has been consumed, so it is not replayed as a duplicate turn
    void removeFollowupByBacklogId(const std::string &sessionKey, const std::string &backlogId);

    int clear(const std::string &sessionKey);

    bool hasPending(const std::string &sessionKey) const;
    bool hasInterrupt(const std::string &sessionKey) const;

    size_t depth(const std::string &sessionKey) const;

    int droppedCount(const std::string &sessionKey) const;
    std::vector<std::string> droppedSummaryLines(const std::string &sessionKey) const;
    void resetDroppedState(const std::string &sessionKey);

    static std::string buildCollectPrompt(const std::vector<QueuedItem> &items);
    static std::string buildSummaryPrompt(int droppedCount, const std::vector<std::string> &summaryLines);

    bool waitDebounce(const std::string &sessionKey, int debounceMs);

    void remove(const std::string &sessionKey);

private:
    struct SessionQueueState
    {
        std::deque<QueuedItem> items;
        QueueMode mode = QueueMode::Collect;
        int debounceMs = DEFAULT_DEBOUNCE_MS;
        int cap = DEFAULT_CAP;
        QueueDropPolicy dropPolicy = QueueDropPolicy::Summarize;
        std::chrono::steady_clock::time_point lastEnqueuedAt;
        int droppedCount = 0;
        std::vector<std::string> summaryLines;
    };

    std::map<std::string, SessionQueueState> queues;
    mutable std::mutex mutex;
    std::condition_variable cv;

    SessionQueueState &getOrCreate(const std::string &sessionKey, const QueueSettings &settings);
    bool applyDropPolicy(SessionQueueState &state, const std::string &content);
};

} // namespace bus
} // namespace ionclaw
