#include "ionclaw/bus/MessageBus.hpp"

#include <chrono>
#include <functional>

#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace bus
{

std::string MessageBus::dedupKey(const InboundMessage &msg)
{
    return msg.channel + ":" + msg.chatId + ":" + msg.senderId + ":" + std::to_string(std::hash<std::string>{}(msg.content));
}

bool MessageBus::isDuplicate(const InboundMessage &msg)
{
    // synthetic messages (e.g. wake-on-settle) are never deduplicated
    if (msg.metadata.contains("synthetic") && msg.metadata["synthetic"].is_boolean() && msg.metadata["synthetic"].get<bool>())
    {
        return false;
    }

    purgeExpiredDedup();

    // test only, since the key is recorded once the message is accepted so a rejected retry is not swallowed
    auto it = recentInbound.find(dedupKey(msg));

    if (it != recentInbound.end())
    {
        spdlog::debug("[MessageBus] Dropping duplicate inbound message: {}", dedupKey(msg));
        return true;
    }

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

    // test only, since the timestamp is recorded on accept so a message dropped by a later check does not burn a rate slot
    return times.size() >= MAX_PER_SESSION_IN_WINDOW;
}

void MessageBus::recordAccepted(const InboundMessage &msg)
{
    auto now = std::chrono::steady_clock::now();

    // synthetic messages skip dedup, matching isDuplicate, but still count against nothing here since they were never tested
    if (!(msg.metadata.contains("synthetic") && msg.metadata["synthetic"].is_boolean() && msg.metadata["synthetic"].get<bool>()))
    {
        recentInbound[dedupKey(msg)] = now;
        sessionSendTimes[msg.channel + ":" + msg.chatId].push_back(now);
    }
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
            spdlog::warn("[MessageBus] Session {}:{} exceeded the per-session rate limit, rejecting message", msg.channel, msg.chatId);
            return PublishResult::QueueFull;
        }

        // reject once the backlog is saturated so a flood cannot grow memory without bound
        if (inboundQueue.size() >= MAX_INBOUND_QUEUE)
        {
            spdlog::warn("[MessageBus] Inbound queue full ({}), rejecting message from {}:{}", inboundQueue.size(), msg.channel, msg.chatId);
            return PublishResult::QueueFull;
        }

        // the message passed every check, so record its dedup and rate state and enqueue it
        recordAccepted(msg);
        inboundQueue.push(msg);
    }

    inboundCv.notify_one();
    return PublishResult::Accepted;
}

void MessageBus::publishOutbound(const OutboundMessage &msg)
{
    {
        std::lock_guard<std::mutex> lock(outboundMutex);
        auto &queue = outboundQueues[msg.channel];

        // bound the per-channel queue so a stalled or dead channel runner cannot grow it without limit
        if (queue.size() >= MAX_OUTBOUND_QUEUE)
        {
            spdlog::warn("[MessageBus] Outbound queue full ({}) for channel {}, dropping oldest reply", queue.size(), msg.channel);
            queue.pop();
        }

        queue.push(msg);
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

} // namespace bus
} // namespace ionclaw
