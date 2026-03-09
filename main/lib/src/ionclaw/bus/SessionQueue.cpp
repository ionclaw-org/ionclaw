#include "ionclaw/bus/SessionQueue.hpp"

#include <algorithm>

#include "ionclaw/util/StringHelper.hpp"
#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace bus
{

// resolve effective queue settings from config hierarchy
QueueSettings resolveQueueSettings(
    const config::Config &config,
    const std::string &channel,
    std::optional<QueueMode> inlineMode)
{
    QueueSettings settings;

    // start from global config defaults
    auto modeOpt = normalizeQueueMode(config.messages.queue.mode);

    if (modeOpt.has_value())
    {
        settings.mode = modeOpt.value();
    }

    settings.debounceMs = config.messages.queue.debounceMs;
    settings.cap = config.messages.queue.cap;

    auto dropOpt = normalizeQueueDropPolicy(config.messages.queue.dropPolicy);

    if (dropOpt.has_value())
    {
        settings.dropPolicy = dropOpt.value();
    }

    // per-channel override
    auto channelIt = config.messages.queue.byChannel.find(channel);

    if (channelIt != config.messages.queue.byChannel.end())
    {
        auto channelMode = normalizeQueueMode(channelIt->second);

        if (channelMode.has_value())
        {
            settings.mode = channelMode.value();
        }
    }

    // inline mode takes highest priority
    if (inlineMode.has_value())
    {
        settings.mode = inlineMode.value();
    }

    return settings;
}

// get or create per-session queue state
SessionQueue::SessionQueueState &SessionQueue::getOrCreate(
    const std::string &sessionKey, const QueueSettings &settings)
{
    auto it = queues_.find(sessionKey);

    if (it != queues_.end())
    {
        return it->second;
    }

    auto &state = queues_[sessionKey];
    state.mode = settings.mode;
    state.debounceMs = settings.debounceMs;
    state.cap = settings.cap;
    state.dropPolicy = settings.dropPolicy;
    state.lastEnqueuedAt = std::chrono::steady_clock::now();
    return state;
}

// apply drop policy when queue is at cap
// returns true if the new message should be accepted
bool SessionQueue::applyDropPolicy(SessionQueueState &state, const std::string &content)
{
    if (static_cast<int>(state.items.size()) < state.cap)
    {
        return true;
    }

    switch (state.dropPolicy)
    {
    case QueueDropPolicy::New:
        spdlog::debug("[SessionQueue] Rejecting new message (cap {} reached, drop=new)", state.cap);
        return false;

    case QueueDropPolicy::Old:
    {
        if (!state.items.empty())
        {
            state.items.pop_front();
            state.droppedCount++;
        }

        return true;
    }

    case QueueDropPolicy::Summarize:
    {
        if (!state.items.empty())
        {
            auto summary = state.items.front().message.content;

            // collapse whitespace and truncate to 160 chars
            std::replace(summary.begin(), summary.end(), '\n', ' ');

            if (summary.size() > 160)
            {
                summary = ionclaw::util::StringHelper::utf8SafeTruncate(summary, 157) + "...";
            }

            state.summaryLines.push_back(summary);
            state.items.pop_front();
            state.droppedCount++;
        }

        return true;
    }
    }

    return true;
}

bool SessionQueue::enqueue(const std::string &sessionKey, const InboundMessage &msg,
                           QueueMode mode, const QueueSettings &settings)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto &state = getOrCreate(sessionKey, settings);

    // apply drop policy for non-steer messages
    if (mode != QueueMode::Steer)
    {
        if (!applyDropPolicy(state, msg.content))
        {
            return false;
        }
    }

    QueuedItem item;
    item.message = msg;
    item.mode = mode;
    item.enqueuedAt = std::chrono::steady_clock::now();

    state.items.push_back(std::move(item));
    state.lastEnqueuedAt = std::chrono::steady_clock::now();

    spdlog::debug("[SessionQueue] Enqueued {} message for {} (depth: {})",
                  queueModeToString(mode), sessionKey, state.items.size());

    // wake debounce waiters
    cv_.notify_all();

    return true;
}

std::vector<QueuedItem> SessionQueue::drainSteer(const std::string &sessionKey)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = queues_.find(sessionKey);

    if (it == queues_.end())
    {
        return {};
    }

    std::vector<QueuedItem> result;
    auto &items = it->second.items;

    items.erase(
        std::remove_if(items.begin(), items.end(),
                       [&](const QueuedItem &item)
                       {
                           if (item.mode == QueueMode::Steer)
                           {
                               result.push_back(item);
                               return true;
                           }

                           return false;
                       }),
        items.end());

    if (items.empty() && it->second.droppedCount == 0)
    {
        queues_.erase(it);
    }

    return result;
}

std::vector<QueuedItem> SessionQueue::drainFollowup(const std::string &sessionKey)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = queues_.find(sessionKey);

    if (it == queues_.end())
    {
        return {};
    }

    std::vector<QueuedItem> result;
    auto &items = it->second.items;

    items.erase(
        std::remove_if(items.begin(), items.end(),
                       [&](const QueuedItem &item)
                       {
                           if (item.mode == QueueMode::Followup || item.mode == QueueMode::Collect)
                           {
                               result.push_back(item);
                               return true;
                           }

                           return false;
                       }),
        items.end());

    if (items.empty() && it->second.droppedCount == 0)
    {
        queues_.erase(it);
    }

    return result;
}

int SessionQueue::clear(const std::string &sessionKey)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = queues_.find(sessionKey);

    if (it == queues_.end())
    {
        return 0;
    }

    int count = static_cast<int>(it->second.items.size());
    queues_.erase(it);

    spdlog::debug("[SessionQueue] Cleared {} items for {}", count, sessionKey);

    // wake debounce waiters so they can exit
    cv_.notify_all();

    return count;
}

bool SessionQueue::hasPending(const std::string &sessionKey) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = queues_.find(sessionKey);

    if (it == queues_.end())
    {
        return false;
    }

    return !it->second.items.empty();
}

bool SessionQueue::hasInterrupt(const std::string &sessionKey) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = queues_.find(sessionKey);

    if (it == queues_.end())
    {
        return false;
    }

    for (const auto &item : it->second.items)
    {
        if (item.mode == QueueMode::Interrupt)
        {
            return true;
        }
    }

    return false;
}

size_t SessionQueue::depth(const std::string &sessionKey) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = queues_.find(sessionKey);

    if (it == queues_.end())
    {
        return 0;
    }

    return it->second.items.size();
}

int SessionQueue::droppedCount(const std::string &sessionKey) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = queues_.find(sessionKey);

    if (it == queues_.end())
    {
        return 0;
    }

    return it->second.droppedCount;
}

std::vector<std::string> SessionQueue::droppedSummaryLines(const std::string &sessionKey) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = queues_.find(sessionKey);

    if (it == queues_.end())
    {
        return {};
    }

    return it->second.summaryLines;
}

void SessionQueue::resetDroppedState(const std::string &sessionKey)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = queues_.find(sessionKey);

    if (it == queues_.end())
    {
        return;
    }

    it->second.droppedCount = 0;
    it->second.summaryLines.clear();
}

std::string SessionQueue::buildCollectPrompt(const std::vector<QueuedItem> &items)
{
    if (items.empty())
    {
        return "";
    }

    std::string result = "[Queued messages while agent was busy]";

    for (size_t i = 0; i < items.size(); ++i)
    {
        result += "\n\n---\nQueued #" + std::to_string(i + 1) + "\n" + items[i].message.content;
    }

    return result;
}

std::string SessionQueue::buildSummaryPrompt(int droppedCount,
                                             const std::vector<std::string> &summaryLines)
{
    if (droppedCount <= 0)
    {
        return "";
    }

    std::string result = "[Queue overflow] Dropped " + std::to_string(droppedCount) +
                         " message" + (droppedCount > 1 ? "s" : "") + " due to cap.";

    if (!summaryLines.empty())
    {
        result += "\nSummary:";

        for (const auto &line : summaryLines)
        {
            result += "\n- " + line;
        }
    }

    return result;
}

bool SessionQueue::waitDebounce(const std::string &sessionKey, int debounceMs)
{
    auto deadline = std::chrono::milliseconds(debounceMs);

    while (true)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        auto it = queues_.find(sessionKey);

        if (it == queues_.end())
        {
            // queue was cleared (interrupt)
            return false;
        }

        auto elapsed = std::chrono::steady_clock::now() - it->second.lastEnqueuedAt;

        if (elapsed >= deadline)
        {
            return true;
        }

        auto remaining = deadline - std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);

        cv_.wait_for(lock, remaining);
    }
}

void SessionQueue::wakeDebounce()
{
    cv_.notify_all();
}

void SessionQueue::remove(const std::string &sessionKey)
{
    std::lock_guard<std::mutex> lock(mutex_);
    queues_.erase(sessionKey);
}

} // namespace bus
} // namespace ionclaw
