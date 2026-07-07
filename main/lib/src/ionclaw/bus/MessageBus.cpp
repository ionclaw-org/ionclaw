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
}

PublishResult MessageBus::publishInbound(const InboundMessage &msg)
{
    {
        std::lock_guard<std::mutex> lock(inboundMutex);

        if (isDuplicate(msg))
        {
            return PublishResult::Duplicate;
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
        outboundQueue.push(msg);
    }

    outboundCv.notify_one();
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

bool MessageBus::consumeOutbound(OutboundMessage &msg, int timeoutMs)
{
    std::unique_lock<std::mutex> lock(outboundMutex);

    // clang-format off
    if (!outboundCv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this]() { return !outboundQueue.empty(); }))
    // clang-format on
    {
        return false;
    }

    msg = std::move(outboundQueue.front());
    outboundQueue.pop();

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
    return outboundQueue.size();
}

} // namespace bus
} // namespace ionclaw
