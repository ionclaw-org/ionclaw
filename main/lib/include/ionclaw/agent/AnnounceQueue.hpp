#pragma once

#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace ionclaw
{
namespace agent
{

struct AnnounceEntry
{
    std::string runId;
    std::string announceId; // idempotency key: childSessionKey:childRunId
    std::string requesterSessionKey;
    std::string message;
    int retries = 0;
    std::chrono::steady_clock::time_point nextRetryAt;
    std::chrono::steady_clock::time_point expiresAt;
};

class AnnounceQueue
{
public:
    static constexpr int MAX_RETRIES = 3;
    static constexpr int EXPIRY_SECONDS = 300;

    static std::string buildAnnounceId(const std::string &childSessionKey, const std::string &childRunId);

    bool enqueue(const std::string &runId, const std::string &requesterSessionKey,
                 const std::string &message, const std::string &announceId = "");
    std::vector<AnnounceEntry> drain(const std::string &sessionKey);
    void markRetry(const std::string &runId);
    void processExpired();

private:
    std::vector<AnnounceEntry> entries;
    std::map<std::string, std::chrono::steady_clock::time_point> seenIds; // idempotency tracking
    mutable std::mutex mutex;

    static constexpr int SEEN_ID_TTL_SECONDS = 600;
};

} // namespace agent
} // namespace ionclaw
