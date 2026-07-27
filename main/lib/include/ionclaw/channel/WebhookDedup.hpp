#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>

namespace ionclaw
{
namespace channel
{

// idempotency store for inbound webhook message ids, so a provider redelivery is processed at most once
class WebhookDedup
{
public:
    // returns true if this id is new (process it); false if it was already seen within the retention window
    bool markSeen(const std::string &msgId);

    size_t size() const;

private:
    void prune(std::chrono::steady_clock::time_point now);

    // ids expire after this window and the store is capped, so a flood cannot grow it without bound
    static constexpr int RETENTION_SECONDS = 24 * 60 * 60;
    static constexpr size_t MAX_ENTRIES = 100000;

    mutable std::mutex mutex;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> seen;
    std::deque<std::pair<std::string, std::chrono::steady_clock::time_point>> order;
};

} // namespace channel
} // namespace ionclaw
