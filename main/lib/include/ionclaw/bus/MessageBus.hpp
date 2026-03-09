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

class MessageBus
{
public:
    void publishInbound(const InboundMessage &msg);
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

    // inbound message deduplication (TTL cache)
    static constexpr int DEDUP_TTL_SECONDS = 5;
    std::map<std::string, std::chrono::steady_clock::time_point> recentInbound;
    bool isDuplicate(const InboundMessage &msg);
    void purgeExpiredDedup();
};

} // namespace bus
} // namespace ionclaw
