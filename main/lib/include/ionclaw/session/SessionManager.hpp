#pragma once

#include <atomic>
#include <chrono>
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
    int lastConsolidated = 0;
    std::vector<SessionMessage> messages;
    nlohmann::json liveState;

    // abort/resume tracking
    bool abortedLastRun = false;
    int abortCutoffMessageIndex = -1;
    std::string abortCutoffTimestamp;
};

class SessionManager
{
public:
    explicit SessionManager(const std::string &sessionsDir, int64_t maxDiskBytes = 0, double highWaterRatio = 0.8);

    // returns a COPY of the session (safe from eviction)
    Session getOrCreate(const std::string &sessionKey);

    // ensure session exists without returning a copy
    void ensureSession(const std::string &sessionKey);

    void addMessage(const std::string &sessionKey, const SessionMessage &message);
    std::vector<SessionMessage> getHistory(const std::string &sessionKey, int maxMessages = 500);
    std::vector<SessionInfo> listSessions();
    void deleteSession(const std::string &sessionKey);
    void clearSession(const std::string &sessionKey);
    void save(const std::string &sessionKey);

    void setAbortCutoffAll();
    void clearAbortFlag(const std::string &sessionKey);

    // targeted mutations (thread-safe, no dangling reference)
    void updateLiveStateField(const std::string &sessionKey, const std::string &field, const nlohmann::json &value);
    void updateLastMessageContent(const std::string &sessionKey, const std::string &content);
    void setLastConsolidated(const std::string &sessionKey, int count);

    // lru configuration (only call before threads start or use atomic)
    void setMaxCapacity(int capacity) { maxCapacity.store(capacity); }
    void setIdleTtlSeconds(int ttl) { idleTtlSeconds.store(ttl); }
    void setMaxCreationsPerMinute(int limit) { maxCreationsPerMinute.store(limit); }

private:
    std::string sessionsDir;
    std::map<std::string, Session> cache;
    std::map<std::string, std::unique_ptr<std::mutex>> sessionMutexes;
    mutable std::mutex globalMutex;

    std::mutex &getSessionMutex(const std::string &key);
    void loadFromDisk(const std::string &sessionKey);
    void writeSessionFile(const Session &session); // write session to disk (no locking)
    std::string sessionFilePath(const std::string &sessionKey) const;
    std::string sanitizeFilename(const std::string &key) const;

    // internal: returns reference under globalMutex (caller must hold both locks)
    Session &getOrCreateLocked(const std::string &sessionKey);

    void touch(Session &session);
    void evictIfNeeded();
    void reapIdleSessions();
    void reapIdleSessionsLocked(std::vector<Session> &outSnapshots); // assumes globalMutex is already held
    bool checkRateLimit();

    // active session filenames for sweeper coordination
    std::vector<std::string> getActiveFilenames() const;

    SessionSweeper sweeper;
    std::atomic<int> messageCounter{0};
    static constexpr int SWEEP_INTERVAL = 50;
    static constexpr int MAX_MESSAGE_PERSIST_CHARS = 500000;

    // lru eviction
    std::atomic<int> maxCapacity{5000};
    std::atomic<int> idleTtlSeconds{86400};

    // rate limiting (fixed-window, per minute)
    std::atomic<int> maxCreationsPerMinute{0};
    std::deque<std::chrono::steady_clock::time_point> creationTimestamps;
};

} // namespace session
} // namespace ionclaw
