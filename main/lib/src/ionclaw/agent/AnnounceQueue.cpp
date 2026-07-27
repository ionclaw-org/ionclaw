#include "ionclaw/agent/AnnounceQueue.hpp"

#include <algorithm>

#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace agent
{

std::string AnnounceQueue::buildAnnounceId(const std::string &childSessionKey, const std::string &childRunId)
{
    return "v1:" + childSessionKey + ":" + childRunId;
}

bool AnnounceQueue::enqueue(const std::string &runId, const std::string &requesterSessionKey, const std::string &message, const std::string &announceId)
{
    std::lock_guard<std::mutex> lock(mutex);

    auto now = std::chrono::steady_clock::now();

    // idempotency check: reject duplicate announcements
    if (!announceId.empty())
    {
        auto it = seenIds.find(announceId);

        if (it != seenIds.end() && it->second > now)
        {
            spdlog::debug("[AnnounceQueue] Duplicate announce '{}' for run {}, skipping", announceId, runId);
            return false;
        }

        seenIds[announceId] = now + std::chrono::seconds(SEEN_ID_TTL_SECONDS);
    }

    AnnounceEntry entry;
    entry.runId = runId;
    entry.announceId = announceId;
    entry.requesterSessionKey = requesterSessionKey;
    entry.message = message;
    entry.retries = 0;
    entry.nextRetryAt = now;
    entry.expiresAt = now + std::chrono::seconds(EXPIRY_SECONDS);

    entries.push_back(std::move(entry));

    spdlog::debug("[AnnounceQueue] Enqueued announce for run {} to session {}", runId, requesterSessionKey);
    return true;
}

std::vector<AnnounceEntry> AnnounceQueue::drain(const std::string &sessionKey)
{
    std::lock_guard<std::mutex> lock(mutex);

    auto now = std::chrono::steady_clock::now();
    std::vector<AnnounceEntry> result;

    // clang-format off
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const AnnounceEntry &e) {
        if (e.requesterSessionKey == sessionKey && e.nextRetryAt <= now)
        {
            result.push_back(e);
            return true;
        }

        return false;
    }), entries.end());
    // clang-format on

    return result;
}

void AnnounceQueue::processExpired()
{
    std::lock_guard<std::mutex> lock(mutex);

    auto now = std::chrono::steady_clock::now();
    auto before = entries.size();

    // clang-format off
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const AnnounceEntry &e) {
        if (e.expiresAt <= now)
        {
            spdlog::debug("[AnnounceQueue] Announce for run {} expired", e.runId);
            return true;
        }

        if (e.retries > MAX_RETRIES)
        {
            return true;
        }

        return false;
    }), entries.end());
    // clang-format on

    auto removed = before - entries.size();

    if (removed > 0)
    {
        spdlog::info("[AnnounceQueue] Expired {} announce entries", removed);
    }

    // cleanup expired idempotency keys
    for (auto it = seenIds.begin(); it != seenIds.end();)
    {
        if (it->second <= now)
        {
            it = seenIds.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

} // namespace agent
} // namespace ionclaw
