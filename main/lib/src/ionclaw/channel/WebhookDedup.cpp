#include "ionclaw/channel/WebhookDedup.hpp"

namespace ionclaw
{
namespace channel
{

void WebhookDedup::prune(std::chrono::steady_clock::time_point now)
{
    auto cutoff = now - std::chrono::seconds(RETENTION_SECONDS);

    // drop entries past the retention window, and cap the total by evicting the oldest
    while (!order.empty() && (order.front().second < cutoff || seen.size() > MAX_ENTRIES))
    {
        auto it = seen.find(order.front().first);

        // only erase if this deque record is the one that owns the current timestamp
        if (it != seen.end() && it->second == order.front().second)
        {
            seen.erase(it);
        }

        order.pop_front();
    }
}

bool WebhookDedup::markSeen(const std::string &msgId)
{
    if (msgId.empty())
    {
        // no id to key on, so treat as new and let downstream decide
        return true;
    }

    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mutex);

    prune(now);

    if (seen.find(msgId) != seen.end())
    {
        return false;
    }

    seen[msgId] = now;
    order.emplace_back(msgId, now);
    return true;
}

size_t WebhookDedup::size() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return seen.size();
}

} // namespace channel
} // namespace ionclaw
