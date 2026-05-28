#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "ionclaw/session/SessionSweeper.hpp"

namespace ionclaw
{
namespace session
{

struct SessionMessage
{
    std::string role;
    std::string content;
    std::string timestamp;
    std::string agentName;
    std::vector<nlohmann::json> media;
    nlohmann::json raw;

    nlohmann::json toJson() const;
    static SessionMessage fromJson(const nlohmann::json &j);
};

struct SessionInfo
{
    std::string key;
    std::string createdAt;
    std::string updatedAt;
    std::string displayName;
    std::string channel;
};

struct Session
{
    std::string key;
    std::string createdAt;
    std::string updatedAt;
    std::string lastTouchedAt;
    std::string displayName;
    std::vector<SessionMessage> messages;
    nlohmann::json liveState;

    bool abortedLastRun = false;
    int abortCutoffMessageIndex = -1;
    std::string abortCutoffTimestamp;

    // transient flag: the user stopped the previous run, consumed once to prime the next turn
    bool stoppedByUser = false;
};

class SessionManager
{
public:
    explicit SessionManager(const std::string &sessionsDir, int64_t maxDiskBytes = 0, double highWaterRatio = 0.8);

    Session getOrCreate(const std::string &sessionKey);
    void ensureSession(const std::string &sessionKey);

    bool addMessage(const std::string &sessionKey, const SessionMessage &message);
    std::vector<SessionMessage> getHistory(const std::string &sessionKey, int maxMessages = 500);
    std::vector<SessionInfo> listSessions();
    void deleteSession(const std::string &sessionKey);
    void clearSession(const std::string &sessionKey);
    void save(const std::string &sessionKey);

    void setAbortCutoffAll();
    void clearAbortFlag(const std::string &sessionKey);

    void markStoppedByUser(const std::string &sessionKey);
    void clearStoppedByUser(const std::string &sessionKey);

    void updateLiveStateField(const std::string &sessionKey, const std::string &field, const nlohmann::json &value);
    void updateLastMessageContent(const std::string &sessionKey, const std::string &content);

    void setMaxCapacity(int capacity) { maxCapacity.store(capacity); }
    void setIdleTtlSeconds(int ttl) { idleTtlSeconds.store(ttl); }
    void setMaxCreationsPerMinute(int limit) { maxCreationsPerMinute.store(limit); }

private:
    std::string sessionsDir;
    std::map<std::string, Session> cache;
    std::map<std::string, std::shared_ptr<std::mutex>> sessionMutexes;
    mutable std::mutex globalMutex;

    std::shared_ptr<std::mutex> getSessionMutex(const std::string &key);
    void loadFromDisk(const std::string &sessionKey);
    void writeSessionFile(const Session &session);
    std::string sessionFilePath(const std::string &sessionKey) const;
    std::string sanitizeFilename(const std::string &key) const;

    Session &getOrCreateLocked(const std::string &sessionKey);

    void touch(Session &session);
    void evictIfNeeded();
    void reapIdleSessionsLocked(std::vector<Session> &outSnapshots);
    bool checkRateLimitLocked();

    std::vector<std::string> getActiveFilenames() const;

    SessionSweeper sweeper;
    std::atomic<int64_t> messageCounter{0};
    static constexpr int SWEEP_INTERVAL = 50;
    static constexpr int MAX_MESSAGE_PERSIST_CHARS = 500000;

    std::atomic<int> maxCapacity{5000};
    std::atomic<int> idleTtlSeconds{86400};

    std::atomic<int> maxCreationsPerMinute{0};
    std::deque<std::chrono::steady_clock::time_point> creationTimestamps;
};

} // namespace session
} // namespace ionclaw
