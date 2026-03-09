#include "ionclaw/session/SessionManager.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "ionclaw/util/StringHelper.hpp"
#include "ionclaw/util/TimeHelper.hpp"
#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace session
{

namespace fs = std::filesystem;

// session message serialization

nlohmann::json SessionMessage::toJson() const
{
    nlohmann::json j;
    j["role"] = role;
    j["content"] = content;
    j["timestamp"] = timestamp;

    if (!agentName.empty())
    {
        j["agent_name"] = agentName;
    }

    if (!media.empty())
    {
        j["media"] = media;
    }

    if (!raw.is_null())
    {
        j["raw"] = raw;
    }

    return j;
}

SessionMessage SessionMessage::fromJson(const nlohmann::json &j)
{
    SessionMessage msg;
    msg.role = j.value("role", "");

    // content can be a string or an array of content blocks (Anthropic format)
    if (j.contains("content"))
    {
        if (j["content"].is_string())
        {
            msg.content = j["content"].get<std::string>();
        }
        else if (j["content"].is_array())
        {
            // extract text from content blocks: [{"type":"text","text":"..."}]
            std::string combined;

            for (const auto &block : j["content"])
            {
                if (block.is_object() && block.value("type", "") == "text")
                {
                    if (!combined.empty())
                    {
                        combined += "\n";
                    }

                    combined += block.value("text", "");
                }
            }

            msg.content = combined;
        }
    }

    msg.timestamp = j.value("timestamp", "");
    msg.agentName = j.value("agent_name", "");

    if (j.contains("media") && j["media"].is_array())
    {
        msg.media = j["media"].get<std::vector<nlohmann::json>>();
    }

    if (j.contains("raw"))
    {
        msg.raw = j["raw"];
    }

    return msg;
}

// session manager implementation

SessionManager::SessionManager(const std::string &sessionsDir, int64_t maxDiskBytes, double highWaterRatio)
    : sessionsDir(sessionsDir)
    , sweeper(sessionsDir, maxDiskBytes, highWaterRatio)
{
    std::error_code ec;
    fs::create_directories(sessionsDir, ec);

    if (ec)
    {
        spdlog::error("Failed to create sessions directory {}: {}", sessionsDir, ec.message());
    }
}

std::mutex &SessionManager::getSessionMutex(const std::string &key)
{
    std::lock_guard<std::mutex> lock(globalMutex);

    auto it = sessionMutexes.find(key);

    if (it == sessionMutexes.end())
    {
        sessionMutexes[key] = std::make_unique<std::mutex>();
        return *sessionMutexes[key];
    }

    return *it->second;
}

// internal: returns reference under caller-held locks
Session &SessionManager::getOrCreateLocked(const std::string &sessionKey)
{
    auto it = cache.find(sessionKey);

    if (it != cache.end())
    {
        touch(it->second);
        return it->second;
    }

    auto filePath = sessionFilePath(sessionKey);

    if (fs::exists(filePath))
    {
        loadFromDisk(sessionKey);
    }
    else
    {
        Session session;
        session.key = sessionKey;
        session.createdAt = util::TimeHelper::now();
        session.updatedAt = session.createdAt;
        session.lastTouchedAt = session.createdAt;
        cache[sessionKey] = std::move(session);
    }

    touch(cache[sessionKey]);
    return cache[sessionKey];
}

Session SessionManager::getOrCreate(const std::string &sessionKey)
{
    auto &mtx = getSessionMutex(sessionKey);
    std::lock_guard<std::mutex> lock(mtx);

    {
        std::lock_guard<std::mutex> glock(globalMutex);
        auto it = cache.find(sessionKey);

        if (it != cache.end())
        {
            touch(it->second);
            return it->second; // return copy
        }
    }

    // new session: check rate limit and capacity
    if (maxCreationsPerMinute.load() > 0 && !checkRateLimit())
    {
        spdlog::warn("[SessionManager] Rate limit exceeded for session creation");
    }

    evictIfNeeded();

    std::lock_guard<std::mutex> glock(globalMutex);
    auto &session = getOrCreateLocked(sessionKey);
    return session; // return copy
}

void SessionManager::ensureSession(const std::string &sessionKey)
{
    auto &mtx = getSessionMutex(sessionKey);
    std::lock_guard<std::mutex> lock(mtx);

    {
        std::lock_guard<std::mutex> glock(globalMutex);

        if (cache.find(sessionKey) != cache.end())
        {
            touch(cache[sessionKey]);
            return;
        }
    }

    if (maxCreationsPerMinute.load() > 0 && !checkRateLimit())
    {
        spdlog::warn("[SessionManager] Rate limit exceeded for session creation");
    }

    evictIfNeeded();

    std::lock_guard<std::mutex> glock(globalMutex);
    getOrCreateLocked(sessionKey);
}

void SessionManager::addMessage(const std::string &sessionKey, const SessionMessage &message)
{
    // ensure session exists before acquiring session-level lock
    ensureSession(sessionKey);

    auto &mtx = getSessionMutex(sessionKey);
    std::lock_guard<std::mutex> lock(mtx);

    // cap oversized message content before persisting to disk
    auto cappedMessage = message;

    if (static_cast<int>(cappedMessage.content.size()) > MAX_MESSAGE_PERSIST_CHARS)
    {
        cappedMessage.content = ionclaw::util::StringHelper::utf8SafeTruncate(
                                    cappedMessage.content, MAX_MESSAGE_PERSIST_CHARS) +
                                "\n[content truncated for disk persistence]";
        spdlog::debug("[SessionManager] Capped message content from {}→{} chars",
                      message.content.size(), MAX_MESSAGE_PERSIST_CHARS);
    }

    // hold globalMutex while accessing session to prevent use-after-free from concurrent eviction
    std::string filePath;
    bool fileExists;
    std::string sessionKey_;
    std::string createdAt;
    std::string updatedAt;

    {
        std::lock_guard<std::mutex> glock(globalMutex);

        auto it = cache.find(sessionKey);

        if (it == cache.end())
        {
            // session was evicted between ensureSession and here; re-insert
            Session session;
            session.key = sessionKey;
            session.createdAt = util::TimeHelper::now();
            session.updatedAt = session.createdAt;
            session.lastTouchedAt = session.createdAt;
            cache[sessionKey] = std::move(session);
            it = cache.find(sessionKey);
        }

        auto &session = it->second;
        session.messages.push_back(cappedMessage);
        session.updatedAt = util::TimeHelper::now();

        filePath = sessionFilePath(sessionKey);
        fileExists = fs::exists(filePath);
        sessionKey_ = session.key;
        createdAt = session.createdAt;
        updatedAt = session.updatedAt;
    }

    // file I/O outside globalMutex (per-session mutex still held, prevents concurrent writes)
    std::ofstream ofs(filePath, std::ios::app);

    if (!ofs.is_open())
    {
        spdlog::error("Failed to open session file for append: {}", filePath);
        return;
    }

    if (!fileExists)
    {
        nlohmann::json meta;
        meta["_type"] = "metadata";
        meta["key"] = sessionKey_;
        meta["created_at"] = createdAt;
        meta["updated_at"] = updatedAt;
        ofs << meta.dump() << "\n";
    }

    ofs << cappedMessage.toJson().dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << "\n";
    ofs.flush();

    // periodic disk budget sweep (pass active filenames to avoid deleting live sessions)
    if (++messageCounter % SWEEP_INTERVAL == 0)
    {
        auto activeFiles = getActiveFilenames();
        sweeper.sweepIfNeeded(activeFiles);
    }
}

std::vector<SessionMessage> SessionManager::getHistory(const std::string &sessionKey, int maxMessages)
{
    auto &mtx = getSessionMutex(sessionKey);
    std::lock_guard<std::mutex> lock(mtx);

    std::lock_guard<std::mutex> glock(globalMutex);

    auto it = cache.find(sessionKey);

    if (it == cache.end())
    {
        auto filePath = sessionFilePath(sessionKey);

        if (fs::exists(filePath))
        {
            loadFromDisk(sessionKey);
            it = cache.find(sessionKey);
        }

        if (it == cache.end())
        {
            return {};
        }
    }

    touch(it->second);

    auto &messages = it->second.messages;

    if (static_cast<int>(messages.size()) <= maxMessages)
    {
        return messages;
    }

    auto start = messages.begin() + (static_cast<int>(messages.size()) - maxMessages);
    return std::vector<SessionMessage>(start, messages.end());
}

std::vector<SessionInfo> SessionManager::listSessions()
{
    // snapshot cache keys under lock, then read files without holding the lock
    std::map<std::string, SessionInfo> cachedInfo;

    {
        std::lock_guard<std::mutex> lock(globalMutex);

        for (const auto &[key, session] : cache)
        {
            SessionInfo info;
            info.key = key;
            info.createdAt = session.createdAt;
            info.updatedAt = session.updatedAt;
            cachedInfo[sanitizeFilename(key) + ".jsonl"] = std::move(info);
        }
    }

    // read disk files without holding any locks
    std::vector<SessionInfo> result;
    std::error_code ec;

    for (const auto &entry : fs::directory_iterator(sessionsDir, ec))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".jsonl")
        {
            continue;
        }

        auto filename = entry.path().filename().string();

        // prefer cached info if available
        auto cachedIt = cachedInfo.find(filename);

        if (cachedIt != cachedInfo.end())
        {
            result.push_back(cachedIt->second);
            cachedInfo.erase(cachedIt);
            continue;
        }

        std::ifstream ifs(entry.path());

        if (!ifs.is_open())
        {
            continue;
        }

        std::string firstLine;

        if (!std::getline(ifs, firstLine) || firstLine.empty())
        {
            continue;
        }

        try
        {
            auto j = nlohmann::json::parse(firstLine);

            if (j.value("_type", "") != "metadata")
            {
                continue;
            }

            SessionInfo info;
            info.key = j.value("key", "");
            info.createdAt = j.value("created_at", "");
            info.updatedAt = j.value("updated_at", "");
            info.displayName = j.value("display_name", info.key);
            info.channel = j.value("channel", "");
            result.push_back(std::move(info));
        }
        catch (const nlohmann::json::exception &e)
        {
            spdlog::warn("Failed to parse session metadata from {}: {}", entry.path().string(), e.what());
        }
    }

    // most recent first
    std::sort(result.begin(), result.end(), [](const SessionInfo &a, const SessionInfo &b)
              { return a.updatedAt > b.updatedAt; });

    return result;
}

void SessionManager::deleteSession(const std::string &sessionKey)
{
    auto &mtx = getSessionMutex(sessionKey);
    std::lock_guard<std::mutex> lock(mtx);

    {
        std::lock_guard<std::mutex> glock(globalMutex);
        cache.erase(sessionKey);
    }

    auto filePath = sessionFilePath(sessionKey);
    std::error_code ec;

    if (fs::exists(filePath))
    {
        fs::remove(filePath, ec);

        if (ec)
        {
            spdlog::error("Failed to delete session file {}: {}", filePath, ec.message());
        }
    }
}

void SessionManager::clearSession(const std::string &sessionKey)
{
    auto &mtx = getSessionMutex(sessionKey);
    std::lock_guard<std::mutex> lock(mtx);

    // collect data under globalMutex, then do file I/O outside
    std::string filePath;
    std::string key;
    std::string createdAt;
    std::string updatedAt;

    {
        std::lock_guard<std::mutex> glock(globalMutex);
        auto it = cache.find(sessionKey);

        if (it != cache.end())
        {
            it->second.messages.clear();
            it->second.lastConsolidated = 0;
            it->second.updatedAt = util::TimeHelper::now();
            it->second.liveState = nullptr;

            key = it->second.key;
            createdAt = it->second.createdAt;
            updatedAt = it->second.updatedAt;
        }
        else
        {
            // session not in cache, nothing to clear
            return;
        }

        filePath = sessionFilePath(sessionKey);
    }

    if (fs::exists(filePath))
    {
        std::ofstream ofs(filePath, std::ios::trunc);

        if (ofs.is_open())
        {
            nlohmann::json meta;
            meta["_type"] = "metadata";
            meta["key"] = key;
            meta["created_at"] = createdAt;
            meta["updated_at"] = updatedAt;
            ofs << meta.dump() << "\n";
            ofs.flush();
        }
    }
}

// write session data to its JSONL file (caller must ensure thread-safety)
void SessionManager::writeSessionFile(const Session &session)
{
    auto filePath = sessionFilePath(session.key);

    std::ofstream ofs(filePath, std::ios::trunc);

    if (!ofs.is_open())
    {
        spdlog::error("Failed to open session file for save: {}", filePath);
        return;
    }

    // write metadata
    nlohmann::json meta;
    meta["_type"] = "metadata";
    meta["key"] = session.key;
    meta["created_at"] = session.createdAt;
    meta["updated_at"] = session.updatedAt;
    meta["last_consolidated"] = session.lastConsolidated;

    if (!session.liveState.is_null())
    {
        meta["live_state"] = session.liveState;
    }

    if (session.abortedLastRun)
    {
        meta["aborted_last_run"] = true;
        meta["abort_cutoff_index"] = session.abortCutoffMessageIndex;
        meta["abort_cutoff_timestamp"] = session.abortCutoffTimestamp;
    }

    ofs << meta.dump() << "\n";

    // write messages
    for (const auto &msg : session.messages)
    {
        ofs << msg.toJson().dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << "\n";
    }

    ofs.flush();
}

void SessionManager::save(const std::string &sessionKey)
{
    auto &mtx = getSessionMutex(sessionKey);
    std::lock_guard<std::mutex> lock(mtx);

    // copy session under globalMutex, then write outside to avoid holding both locks during I/O
    Session snapshot;
    {
        std::lock_guard<std::mutex> glock(globalMutex);
        auto it = cache.find(sessionKey);

        if (it == cache.end())
        {
            return;
        }

        snapshot = it->second;
    }

    writeSessionFile(snapshot);
}

void SessionManager::setAbortCutoffAll()
{
    // snapshot all sessions under globalMutex, then write outside to minimize lock hold
    // also hold per-session mutexes to prevent concurrent addMessage file appends
    std::vector<std::pair<std::string, Session>> snapshots;

    {
        std::lock_guard<std::mutex> lock(globalMutex);

        auto timestamp = util::TimeHelper::now();

        for (auto &[key, session] : cache)
        {
            session.abortedLastRun = true;
            session.abortCutoffMessageIndex = static_cast<int>(session.messages.size());
            session.abortCutoffTimestamp = timestamp;
            snapshots.push_back({key, session});
        }
    }

    // write each session file while holding its per-session mutex
    for (const auto &[key, snapshot] : snapshots)
    {
        auto &mtx = getSessionMutex(key);
        std::lock_guard<std::mutex> lock(mtx);
        writeSessionFile(snapshot);
    }

    if (!snapshots.empty())
    {
        spdlog::info("[SessionManager] Set abort cutoff on {} sessions", static_cast<int>(snapshots.size()));
    }
}

void SessionManager::clearAbortFlag(const std::string &sessionKey)
{
    auto &mtx = getSessionMutex(sessionKey);
    std::lock_guard<std::mutex> lock(mtx);

    // mutate and snapshot under globalMutex, write outside
    Session snapshot;
    {
        std::lock_guard<std::mutex> glock(globalMutex);
        auto it = cache.find(sessionKey);

        if (it == cache.end())
        {
            return;
        }

        it->second.abortedLastRun = false;
        it->second.abortCutoffMessageIndex = -1;
        it->second.abortCutoffTimestamp.clear();

        snapshot = it->second;
    }

    writeSessionFile(snapshot);
}

void SessionManager::updateLiveStateField(const std::string &sessionKey, const std::string &field, const nlohmann::json &value)
{
    auto &mtx = getSessionMutex(sessionKey);
    std::lock_guard<std::mutex> lock(mtx);

    std::lock_guard<std::mutex> glock(globalMutex);

    auto it = cache.find(sessionKey);

    if (it == cache.end())
    {
        return;
    }

    it->second.liveState[field] = value;
}

void SessionManager::updateLastMessageContent(const std::string &sessionKey, const std::string &content)
{
    auto &mtx = getSessionMutex(sessionKey);
    std::lock_guard<std::mutex> lock(mtx);

    std::lock_guard<std::mutex> glock(globalMutex);

    auto it = cache.find(sessionKey);

    if (it == cache.end() || it->second.messages.empty())
    {
        return;
    }

    it->second.messages.back().content = content;
}

void SessionManager::setLastConsolidated(const std::string &sessionKey, int count)
{
    auto &mtx = getSessionMutex(sessionKey);
    std::lock_guard<std::mutex> lock(mtx);

    std::lock_guard<std::mutex> glock(globalMutex);

    auto it = cache.find(sessionKey);

    if (it == cache.end())
    {
        return;
    }

    it->second.lastConsolidated = count;
}

void SessionManager::touch(Session &session)
{
    session.lastTouchedAt = util::TimeHelper::now();
}

void SessionManager::evictIfNeeded()
{
    if (maxCapacity.load() <= 0)
    {
        return;
    }

    // collect sessions to persist, then write outside the lock
    std::vector<Session> toWrite;

    {
        std::lock_guard<std::mutex> glock(globalMutex);

        // reap idle sessions first (collects snapshots into toWrite)
        reapIdleSessionsLocked(toWrite);

        // evict lru if still over capacity
        while (static_cast<int>(cache.size()) >= maxCapacity.load())
        {
            std::string oldestKey;
            std::string oldestTouch;

            for (const auto &[key, session] : cache)
            {
                auto &ts = session.lastTouchedAt.empty() ? session.updatedAt : session.lastTouchedAt;

                if (oldestKey.empty() || ts < oldestTouch)
                {
                    oldestKey = key;
                    oldestTouch = ts;
                }
            }

            if (oldestKey.empty())
            {
                break;
            }

            spdlog::info("[SessionManager] Evicting LRU session: {}", oldestKey);

            toWrite.push_back(cache[oldestKey]);
            cache.erase(oldestKey);
            // do not erase sessionMutexes here: another thread may hold a reference
        }
    }

    // persist evicted sessions outside globalMutex, holding per-session mutex during write
    for (const auto &snapshot : toWrite)
    {
        auto &mtx = getSessionMutex(snapshot.key);
        std::lock_guard<std::mutex> lock(mtx);
        writeSessionFile(snapshot);
    }
}

void SessionManager::reapIdleSessions()
{
    std::vector<Session> toWrite;

    {
        std::lock_guard<std::mutex> glock(globalMutex);
        reapIdleSessionsLocked(toWrite);
    }

    // persist reaped sessions outside globalMutex
    for (const auto &snapshot : toWrite)
    {
        writeSessionFile(snapshot);
    }
}

void SessionManager::reapIdleSessionsLocked(std::vector<Session> &outSnapshots)
{
    if (idleTtlSeconds.load() <= 0)
    {
        return;
    }

    auto now = util::TimeHelper::now();
    std::vector<std::string> toEvict;

    for (const auto &[key, session] : cache)
    {
        auto &ts = session.lastTouchedAt.empty() ? session.updatedAt : session.lastTouchedAt;

        if (ts.empty())
        {
            continue;
        }

        auto diffSeconds = util::TimeHelper::diffSeconds(ts, now);

        if (diffSeconds > idleTtlSeconds.load())
        {
            toEvict.push_back(key);
        }
    }

    for (const auto &key : toEvict)
    {
        spdlog::debug("[SessionManager] Reaping idle session: {}", key);

        auto cit = cache.find(key);

        if (cit != cache.end())
        {
            outSnapshots.push_back(cit->second);
        }

        cache.erase(key);
        // do not erase sessionMutexes: another thread may hold a reference
    }
}

bool SessionManager::checkRateLimit()
{
    if (maxCreationsPerMinute.load() <= 0)
    {
        return true;
    }

    std::lock_guard<std::mutex> lock(globalMutex);

    auto now = std::chrono::steady_clock::now();
    auto windowStart = now - std::chrono::seconds(60);

    // purge old entries
    while (!creationTimestamps.empty() && creationTimestamps.front() < windowStart)
    {
        creationTimestamps.pop_front();
    }

    if (static_cast<int>(creationTimestamps.size()) >= maxCreationsPerMinute.load())
    {
        return false;
    }

    creationTimestamps.push_back(now);
    return true;
}

std::vector<std::string> SessionManager::getActiveFilenames() const
{
    std::lock_guard<std::mutex> lock(globalMutex);

    std::vector<std::string> filenames;
    filenames.reserve(cache.size());

    for (const auto &[key, session] : cache)
    {
        filenames.push_back(sanitizeFilename(key) + ".jsonl");
    }

    return filenames;
}

void SessionManager::loadFromDisk(const std::string &sessionKey)
{
    auto filePath = sessionFilePath(sessionKey);

    std::ifstream ifs(filePath);

    if (!ifs.is_open())
    {
        spdlog::error("Failed to open session file: {}", filePath);
        return;
    }

    Session session;
    session.key = sessionKey;

    std::string line;
    bool firstLine = true;
    int droppedLines = 0;
    std::vector<std::string> validLines;

    while (std::getline(ifs, line))
    {
        if (line.empty())
        {
            continue;
        }

        try
        {
            auto j = nlohmann::json::parse(line);

            if (firstLine && j.value("_type", "") == "metadata")
            {
                session.createdAt = j.value("created_at", "");
                session.updatedAt = j.value("updated_at", "");
                session.lastConsolidated = j.value("last_consolidated", 0);

                if (j.contains("live_state"))
                {
                    session.liveState = j["live_state"];
                }

                session.abortedLastRun = j.value("aborted_last_run", false);
                session.abortCutoffMessageIndex = j.value("abort_cutoff_index", -1);
                session.abortCutoffTimestamp = j.value("abort_cutoff_timestamp", "");

                firstLine = false;
                validLines.push_back(line);
                continue;
            }

            firstLine = false;
            session.messages.push_back(SessionMessage::fromJson(j));
            validLines.push_back(line);
        }
        catch (const nlohmann::json::exception &e)
        {
            droppedLines++;
            spdlog::warn("[SessionManager] Dropped corrupt line in {}: {}", filePath, e.what());
        }
    }

    ifs.close();

    // repair: create backup and rewrite clean file
    if (droppedLines > 0)
    {
        auto bakPath = filePath + ".bak";
        std::error_code ec;

        if (!fs::exists(bakPath))
        {
            fs::copy_file(filePath, bakPath, ec);

            if (ec)
            {
                spdlog::warn("[SessionManager] Failed to create backup {}: {}", bakPath, ec.message());
            }
        }

        std::ofstream ofs(filePath, std::ios::trunc);

        if (ofs.is_open())
        {
            for (const auto &validLine : validLines)
            {
                ofs << validLine << "\n";
            }
        }

        spdlog::warn("[SessionManager] Repaired session {}: dropped {} corrupt lines, backup at {}.bak",
                     sessionKey, droppedLines, filePath);
    }

    if (session.createdAt.empty())
    {
        session.createdAt = util::TimeHelper::now();
    }

    if (session.updatedAt.empty())
    {
        session.updatedAt = session.createdAt;
    }

    cache[sessionKey] = std::move(session);
}

std::string SessionManager::sessionFilePath(const std::string &sessionKey) const
{
    return (fs::path(sessionsDir) / (sanitizeFilename(sessionKey) + ".jsonl")).string();
}

std::string SessionManager::sanitizeFilename(const std::string &key) const
{
    std::string result;
    result.reserve(key.size());

    for (char c : key)
    {
        if (c == ':')
        {
            result += '_';
        }
        else if (c == '/' || c == '\\' || c == '\0' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*')
        {
            // skip dangerous characters
        }
        else
        {
            result += c;
        }
    }

    return result;
}

} // namespace session
} // namespace ionclaw
