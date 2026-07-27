#pragma once

#include "ionclaw/bus/Events.hpp"

#include <chrono>
#include <condition_variable>
#include <deque>
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

    // outbound is routed per channel so each channel runner only receives its own messages and never drops another's
    bool consumeOutbound(const std::string &channel, OutboundMessage &msg, int timeoutMs = 1000);


private:
    std::queue<InboundMessage> inboundQueue;
    std::map<std::string, std::queue<OutboundMessage>> outboundQueues;
    mutable std::mutex inboundMutex;
    mutable std::mutex outboundMutex;
    std::condition_variable inboundCv;
    std::condition_variable outboundCv;

    // bounds inbound memory under a flood by rejecting a burst beyond this with a clear error instead of buffering without limit
    static constexpr size_t MAX_INBOUND_QUEUE = 1000;

    // bounds each channel's outbound backlog so a stalled runner cannot grow it without limit
    static constexpr size_t MAX_OUTBOUND_QUEUE = 1000;
    static constexpr int DEDUP_TTL_SECONDS = 5;
    std::map<std::string, std::chrono::steady_clock::time_point> recentInbound;
    static std::string dedupKey(const InboundMessage &msg);
    bool isDuplicate(const InboundMessage &msg);
    void purgeExpiredDedup();

    // per-session sliding-window rate limit so one session cannot monopolize the shared inbound queue
    static constexpr int RATE_WINDOW_SECONDS = 10;
    static constexpr size_t MAX_PER_SESSION_IN_WINDOW = 30;
    std::map<std::string, std::deque<std::chrono::steady_clock::time_point>> sessionSendTimes;
    bool exceedsSessionRate(const InboundMessage &msg);

    // records dedup and rate-limit state for a message only once it is accepted into the queue
    void recordAccepted(const InboundMessage &msg);
};

} // namespace bus
} // namespace ionclaw
