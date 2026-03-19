#include "ionclaw/session/SessionManager.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
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

std::shared_ptr<std::mutex> SessionManager::getSessionMutex(const std::string &key)
{
    std::lock_guard<std::mutex> lock(globalMutex);

    auto it = sessionMutexes.find(key);

    if (it == sessionMutexes.end())
    {
        auto mtx = std::make_shared<std::mutex>();
        sessionMutexes[key] = mtx;
        return mtx;
    }

    return it->second;
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
    {
        auto mtx = getSessionMutex(sessionKey);
        std::lock_guard<std::mutex> lock(*mtx);
        std::lock_guard<std::mutex> glock(globalMutex);

        auto it = cache.find(sessionKey);

        if (it != cache.end())
        {
            touch(it->second);
            return it->second; // return copy
        }
    }

    // evict outside session mutex to prevent cross-session deadlock
    evictIfNeeded();

    // rate limit check and creation under both locks
    auto mtx = getSessionMutex(sessionKey);
    std::lock_guard<std::mutex> lock(*mtx);
    std::lock_guard<std::mutex> glock(globalMutex);

    // double-check: another thread may have created it while we released the lock
    auto it = cache.find(sessionKey);

    if (it != cache.end())
    {
        touch(it->second);
        return it->second;
    }

    if (maxCreationsPerMinute.load() > 0 && !checkRateLimitLocked())
    {
        spdlog::warn("[SessionManager] Rate limit exceeded for session creation: {}", sessionKey);
        throw std::runtime_error("session creation rate limit exceeded");
    }

    auto &session = getOrCreateLocked(sessionKey);
    return session; // return copy
}

void SessionManager::ensureSession(const std::string &sessionKey)
{
    {
        auto mtx = getSessionMutex(sessionKey);
        std::lock_guard<std::mutex> lock(*mtx);
        std::lock_guard<std::mutex> glock(globalMutex);

        if (cache.find(sessionKey) != cache.end())
        {
            touch(cache[sessionKey]);
            return;
        }
    }

    // evict outside session mutex to prevent cross-session deadlock
    evictIfNeeded();

    // rate limit check and creation under both locks
    auto mtx = getSessionMutex(sessionKey);
    std::lock_guard<std::mutex> lock(*mtx);
    std::lock_guard<std::mutex> glock(globalMutex);

    // double-check: another thread may have created it while we released the lock
    if (cache.find(sessionKey) != cache.end())
    {
        touch(cache[sessionKey]);
        return;
    }

    if (maxCreationsPerMinute.load() > 0 && !checkRateLimitLocked())
    {
        spdlog::warn("[SessionManager] Rate limit exceeded for session creation: {}", sessionKey);
        throw std::runtime_error("session creation rate limit exceeded");
    }

    getOrCreateLocked(sessionKey);
}

bool SessionManager::addMessage(const std::string &sessionKey, const SessionMessage &message)
{
    // ensure session exists before acquiring session-level lock
    ensureSession(sessionKey);

    auto mtx = getSessionMutex(sessionKey);
    std::lock_guard<std::mutex> lock(*mtx);

    // cap oversized message content before persisting to disk
    auto cappedMessage = message;

    if (cappedMessage.content.size() > static_cast<size_t>(MAX_MESSAGE_PERSIST_CHARS))
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
    std::string displayName;
    bool isSubagent = false;

    {
        std::lock_guard<std::mutex> glock(globalMutex);

        auto it = cache.find(sessionKey);

        if (it == cache.end())
        {
            // session was evicted between ensureSession and here; reload from disk to preserve timestamps
            auto filePath = sessionFilePath(sessionKey);

            if (fs::exists(filePath))
            {
                loadFromDisk(sessionKey);
                it = cache.find(sessionKey);
            }

            if (it == cache.end())
            {
                Session session;
                session.key = sessionKey;
                session.createdAt = util::TimeHelper::now();
                session.updatedAt = session.createdAt;
                session.lastTouchedAt = session.createdAt;
                cache[sessionKey] = std::move(session);
                it = cache.find(sessionKey);
            }
        }

        auto &session = it->second;
        session.messages.push_back(cappedMessage);
        session.updatedAt = util::TimeHelper::now();

        // auto-generate display name from the first user message
        if (session.displayName.empty() && cappedMessage.role == "user" && !cappedMessage.content.empty())
        {
            auto preview = ionclaw::util::StringHelper::utf8SafeTruncate(cappedMessage.content, 50);
            // trim to last word boundary if truncated
            if (preview.size() < cappedMessage.content.size())
            {
                auto lastSpace = preview.rfind(' ');
                if (lastSpace != std::string::npos && lastSpace > 10)
                {
                    preview = preview.substr(0, lastSpace);
                }
                preview += "...";
            }
            session.displayName = preview;
        }

        filePath = sessionFilePath(sessionKey);
        fileExists = fs::exists(filePath);
        sessionKey_ = session.key;
        createdAt = session.createdAt;
        updatedAt = session.updatedAt;
        displayName = session.displayName;
        isSubagent = session.liveState.contains("subagent") && session.liveState["subagent"] == true;
    }

    // file I/O outside globalMutex (per-session mutex still held, prevents concurrent writes)
    std::ofstream ofs(filePath, std::ios::app);

    if (!ofs.is_open())
    {
        spdlog::error("[SessionManager] Failed to open session file for append: {}", filePath);
        return false;
    }

    if (!fileExists)
    {
        nlohmann::json meta;
        meta["_type"] = "metadata";
        meta["key"] = sessionKey_;
        meta["created_at"] = createdAt;
        meta["updated_at"] = updatedAt;
        if (!displayName.empty())
        {
            meta["display_name"] = displayName;
        }
        // mark subagent sessions so they can be filtered from session list
        if (isSubagent)
        {
            meta["subagent"] = true;
        }
        ofs << meta.dump() << "\n";
    }

    ofs << cappedMessage.toJson().dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << "\n";
    ofs.flush();

    if (!ofs.good())
    {
        spdlog::error("[SessionManager] Failed to flush session file: {}", filePath);
        return false;
    }

    // periodic disk budget sweep (pass active filenames to avoid deleting live sessions)
    if (++messageCounter % SWEEP_INTERVAL == 0)
    {
        auto activeFiles = getActiveFilenames();
        sweeper.sweepIfNeeded(activeFiles);
    }

    return true;
}

std::vector<SessionMessage> SessionManager::getHistory(const std::string &sessionKey, int maxMessages)
{
    auto mtx = getSessionMutex(sessionKey);
    std::lock_guard<std::mutex> lock(*mtx);

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

    if (maxMessages <= 0 || messages.size() <= static_cast<size_t>(maxMessages))
    {
        return messages;
    }

    auto start = messages.begin() + static_cast<ptrdiff_t>(messages.size() - static_cast<size_t>(maxMessages));
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
            // skip subagent sessions
            if (session.liveState.contains("subagent") && session.liveState["subagent"] == true)
            {
                continue;
            }

            SessionInfo info;
            info.key = key;
            info.createdAt = session.createdAt;
            info.updatedAt = session.updatedAt;
            info.displayName = session.displayName.empty() ? key : session.displayName;
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

            // skip subagent sessions
            if (j.value("subagent", false))
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
    auto mtx = getSessionMutex(sessionKey);
    std::lock_guard<std::mutex> lock(*mtx);

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
    auto mtx = getSessionMutex(sessionKey);
    std::lock_guard<std::mutex> lock(*mtx);

    // collect data under globalMutex, then do file I/O outside
    std::string filePath;
    std::string key;
    std::string createdAt;
    std::string updatedAt;
    std::string dispName;

    {
        std::lock_guard<std::mutex> glock(globalMutex);
        auto it = cache.find(sessionKey);

        if (it != cache.end())
        {
            it->second.messages.clear();
            it->second.updatedAt = util::TimeHelper::now();
            it->second.liveState = nullptr;

            key = it->second.key;
            createdAt = it->second.createdAt;
            updatedAt = it->second.updatedAt;
            dispName = it->second.displayName;
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
            if (!dispName.empty())
            {
                meta["display_name"] = dispName;
            }
            ofs << meta.dump() << "\n";
            ofs.flush();

            if (!ofs.good())
            {
                spdlog::error("[SessionManager] Failed to flush cleared session file: {}", filePath);
            }
        }
        else
        {
            spdlog::error("Failed to open session file for clear: {}", filePath);
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

    if (!session.displayName.empty())
    {
        meta["display_name"] = session.displayName;
    }

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

    if (!ofs.good())
    {
        spdlog::error("[SessionManager] Failed to flush session file: {}", filePath);
    }
}

void SessionManager::save(const std::string &sessionKey)
{
    auto mtx = getSessionMutex(sessionKey);
    std::lock_guard<std::mutex> lock(*mtx);

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
    // collect session keys under globalMutex, then process each under its per-session mutex
    // to prevent concurrent addMessage from appending between snapshot and write
    std::vector<std::string> keys;

    {
        std::lock_guard<std::mutex> lock(globalMutex);

        for (const auto &[key, session] : cache)
        {
            keys.push_back(key);
        }
    }

    auto timestamp = util::TimeHelper::now();

    for (const auto &key : keys)
    {
        auto mtx = getSessionMutex(key);
        std::lock_guard<std::mutex> sessionLock(*mtx);

        Session snapshot;
        {
            std::lock_guard<std::mutex> glock(globalMutex);
            auto it = cache.find(key);

            if (it == cache.end())
            {
                continue;
            }

            it->second.abortedLastRun = true;
            it->second.abortCutoffMessageIndex = static_cast<int>(std::min(it->second.messages.size(), static_cast<size_t>(std::numeric_limits<int>::max())));
            it->second.abortCutoffTimestamp = timestamp;
            snapshot = it->second;
        }

        writeSessionFile(snapshot);
    }

    if (!keys.empty())
    {
        spdlog::info("[SessionManager] Set abort cutoff on {} sessions", keys.size());
    }
}

void SessionManager::clearAbortFlag(const std::string &sessionKey)
{
    auto mtx = getSessionMutex(sessionKey);
    std::lock_guard<std::mutex> lock(*mtx);

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
    auto mtx = getSessionMutex(sessionKey);
    std::lock_guard<std::mutex> lock(*mtx);

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
    auto mtx = getSessionMutex(sessionKey);
    std::lock_guard<std::mutex> lock(*mtx);

    std::lock_guard<std::mutex> glock(globalMutex);

    auto it = cache.find(sessionKey);

    if (it == cache.end() || it->second.messages.empty())
    {
        return;
    }

    it->second.messages.back().content = content;
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
        }
    }

    // persist evicted sessions outside globalMutex, holding per-session mutex during write
    for (const auto &snapshot : toWrite)
    {
        auto mtx = getSessionMutex(snapshot.key);
        std::lock_guard<std::mutex> lock(*mtx);
        writeSessionFile(snapshot);
    }

    // clean up stale mutexes for sessions no longer in cache (safe: callers hold shared_ptr)
    if (!toWrite.empty())
    {
        std::lock_guard<std::mutex> glock(globalMutex);

        for (auto it = sessionMutexes.begin(); it != sessionMutexes.end();)
        {
            if (cache.find(it->first) == cache.end() && it->second.use_count() == 1)
            {
                it = sessionMutexes.erase(it);
            }
            else
            {
                ++it;
            }
        }
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
    }
}

bool SessionManager::checkRateLimitLocked()
{
    if (maxCreationsPerMinute.load() <= 0)
    {
        return true;
    }

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
                session.displayName = j.value("display_name", "");

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
