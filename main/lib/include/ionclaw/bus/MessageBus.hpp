#pragma once

#include "ionclaw/bus/Events.hpp"

#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <queue>
#include <string>

namespace ionclaw
{
namespace bus
{

// outcome of an inbound publish so callers can react instead of silently stranding work
enum class PublishResult
{
    Accepted,
    Duplicate,
    QueueFull
};

class MessageBus
{
public:
    PublishResult publishInbound(const InboundMessage &msg);
    void publishOutbound(const OutboundMessage &msg);

    bool consumeInbound(InboundMessage &msg, int timeoutMs = 1000);
    bool consumeOutbound(OutboundMessage &msg, int timeoutMs = 1000);

    size_t inboundSize() const;
    size_t outboundSize() const;

private:
    std::queue<InboundMessage> inboundQueue;
    std::queue<OutboundMessage> outboundQueue;
    mutable std::mutex inboundMutex;
    mutable std::mutex outboundMutex;
    std::condition_variable inboundCv;
    std::condition_variable outboundCv;

    // bounds inbound memory under a flood; a burst beyond this is rejected with a clear error rather than buffered without limit
    static constexpr size_t MAX_INBOUND_QUEUE = 1000;
    static constexpr int DEDUP_TTL_SECONDS = 5;
    std::map<std::string, std::chrono::steady_clock::time_point> recentInbound;
    bool isDuplicate(const InboundMessage &msg);
    void purgeExpiredDedup();
};

} // namespace bus
} // namespace ionclaw
