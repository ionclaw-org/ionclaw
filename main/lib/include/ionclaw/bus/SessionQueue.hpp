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
    std::string summaryLine;
};

struct QueueSettings
{
    QueueMode mode = QueueMode::Collect;
    int debounceMs = 1000;
    int cap = 20;
    QueueDropPolicy dropPolicy = QueueDropPolicy::Summarize;
};

// resolve effective queue settings from config hierarchy
// priority: inlineMode > byChannel > globalMode > default(collect)
QueueSettings resolveQueueSettings(
    const config::Config &config,
    const std::string &channel,
    std::optional<QueueMode> inlineMode = std::nullopt);

class SessionQueue
{
public:
    static constexpr int DEFAULT_DEBOUNCE_MS = 1000;
    static constexpr int DEFAULT_CAP = 20;

    // enqueue a message for a session with resolved settings
    // returns true if the message was accepted (not dropped by "new" policy)
    bool enqueue(const std::string &sessionKey, const InboundMessage &msg,
                 QueueMode mode, const QueueSettings &settings);

    // drain steer messages (consumed by AgentLoop between iterations)
    std::vector<QueuedItem> drainSteer(const std::string &sessionKey);

    // drain all followup/collect items for processing after turn completes
    std::vector<QueuedItem> drainFollowup(const std::string &sessionKey);

    // clear all pending items for a session (used by interrupt)
    // returns number of cleared items
    int clear(const std::string &sessionKey);

    // check if there are pending items for a session
    bool hasPending(const std::string &sessionKey) const;

    // check if there is an interrupt pending
    bool hasInterrupt(const std::string &sessionKey) const;

    // get queue depth for a session
    size_t depth(const std::string &sessionKey) const;

    // get dropped message count and summary lines for a session
    int droppedCount(const std::string &sessionKey) const;
    std::vector<std::string> droppedSummaryLines(const std::string &sessionKey) const;

    // reset dropped state after drain
    void resetDroppedState(const std::string &sessionKey);

    // build a collect prompt from queued items
    static std::string buildCollectPrompt(const std::vector<QueuedItem> &items);

    // build summary prompt for dropped messages
    static std::string buildSummaryPrompt(int droppedCount,
                                          const std::vector<std::string> &summaryLines);

    // wait for debounce period to elapse since last enqueue
    // returns true if debounce completed, false if interrupted/cleared
    bool waitDebounce(const std::string &sessionKey, int debounceMs);

    // remove queue state for a session entirely
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

    std::map<std::string, SessionQueueState> queues_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;

    SessionQueueState &getOrCreate(const std::string &sessionKey, const QueueSettings &settings);
    bool applyDropPolicy(SessionQueueState &state, const std::string &content);
};

} // namespace bus
} // namespace ionclaw
