#include "ionclaw/bus/MessageBus.hpp"

#include <chrono>
#include <functional>

#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace bus
{

bool MessageBus::isDuplicate(const InboundMessage &msg)
{
    // synthetic messages (e.g. wake-on-settle) are never deduplicated
    if (msg.metadata.contains("synthetic") && msg.metadata["synthetic"].is_boolean() && msg.metadata["synthetic"].get<bool>())
    {
        return false;
    }

    auto key = msg.channel + ":" + msg.chatId + ":" + msg.senderId + ":" + std::to_string(std::hash<std::string>{}(msg.content));

    purgeExpiredDedup();

    auto it = recentInbound.find(key);

    if (it != recentInbound.end())
    {
        spdlog::debug("[MessageBus] Dropping duplicate inbound message: {}", key);
        return true;
    }

    recentInbound[key] = std::chrono::steady_clock::now();
    return false;
}

void MessageBus::purgeExpiredDedup()
{
    auto cutoff = std::chrono::steady_clock::now() - std::chrono::seconds(DEDUP_TTL_SECONDS);

    for (auto it = recentInbound.begin(); it != recentInbound.end();)
    {
        if (it->second < cutoff)
        {
            it = recentInbound.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // drop rate-tracking entries for sessions that have gone quiet so the map stays bounded
    auto rateCutoff = std::chrono::steady_clock::now() - std::chrono::seconds(RATE_WINDOW_SECONDS);

    for (auto it = sessionSendTimes.begin(); it != sessionSendTimes.end();)
    {
        while (!it->second.empty() && it->second.front() < rateCutoff)
        {
            it->second.pop_front();
        }

        if (it->second.empty())
        {
            it = sessionSendTimes.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

bool MessageBus::exceedsSessionRate(const InboundMessage &msg)
{
    // system-generated messages (heartbeat, cron, wake) are not user floods
    if (msg.metadata.contains("synthetic") && msg.metadata["synthetic"].is_boolean() && msg.metadata["synthetic"].get<bool>())
    {
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    auto cutoff = now - std::chrono::seconds(RATE_WINDOW_SECONDS);
    auto &times = sessionSendTimes[msg.channel + ":" + msg.chatId];

    while (!times.empty() && times.front() < cutoff)
    {
        times.pop_front();
    }

    if (times.size() >= MAX_PER_SESSION_IN_WINDOW)
    {
        return true;
    }

    times.push_back(now);
    return false;
}

PublishResult MessageBus::publishInbound(const InboundMessage &msg)
{
    {
        std::lock_guard<std::mutex> lock(inboundMutex);

        if (isDuplicate(msg))
        {
            return PublishResult::Duplicate;
        }

        // reject a single session that is flooding faster than the window allows, so it cannot monopolize the shared queue
        if (exceedsSessionRate(msg))
        {
            spdlog::warn("[MessageBus] session {}:{} exceeded the per-session rate limit, rejecting message", msg.channel, msg.chatId);
            return PublishResult::QueueFull;
        }

        // reject once the backlog is saturated so a flood cannot grow memory without bound
        if (inboundQueue.size() >= MAX_INBOUND_QUEUE)
        {
            spdlog::warn("[MessageBus] inbound queue full ({}), rejecting message from {}:{}", inboundQueue.size(), msg.channel, msg.chatId);
            return PublishResult::QueueFull;
        }

        inboundQueue.push(msg);
    }

    inboundCv.notify_one();
    return PublishResult::Accepted;
}

void MessageBus::publishOutbound(const OutboundMessage &msg)
{
    {
        std::lock_guard<std::mutex> lock(outboundMutex);
        outboundQueues[msg.channel].push(msg);
    }

    // wake every waiter so the consumer for this channel re-checks its own queue
    outboundCv.notify_all();
}

bool MessageBus::consumeInbound(InboundMessage &msg, int timeoutMs)
{
    std::unique_lock<std::mutex> lock(inboundMutex);

    // clang-format off
    if (!inboundCv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this]() { return !inboundQueue.empty(); }))
    // clang-format on
    {
        return false;
    }

    msg = std::move(inboundQueue.front());
    inboundQueue.pop();

    return true;
}

bool MessageBus::consumeOutbound(const std::string &channel, OutboundMessage &msg, int timeoutMs)
{
    std::unique_lock<std::mutex> lock(outboundMutex);

    // clang-format off
    if (!outboundCv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this, &channel]() {
        auto it = outboundQueues.find(channel);
        return it != outboundQueues.end() && !it->second.empty();
    }))
    // clang-format on
    {
        return false;
    }

    auto &queue = outboundQueues[channel];
    msg = std::move(queue.front());
    queue.pop();

    return true;
}

size_t MessageBus::inboundSize() const
{
    std::lock_guard<std::mutex> lock(inboundMutex);
    return inboundQueue.size();
}

size_t MessageBus::outboundSize() const
{
    std::lock_guard<std::mutex> lock(outboundMutex);

    size_t total = 0;

    for (const auto &[channel, queue] : outboundQueues)
    {
        total += queue.size();
    }

    return total;
}

} // namespace bus
} // namespace ionclaw
