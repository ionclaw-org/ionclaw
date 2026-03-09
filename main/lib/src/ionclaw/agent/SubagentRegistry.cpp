#include "ionclaw/agent/SubagentRegistry.hpp"

#include <filesystem>
#include <fstream>

#include "ionclaw/util/StringHelper.hpp"
#include "ionclaw/util/TimeHelper.hpp"
#include "ionclaw/util/UniqueId.hpp"
#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace agent
{

namespace fs = std::filesystem;

// SubagentRunRecord serialization

std::string SubagentRunRecord::statusToString(SubagentStatus s)
{
    switch (s)
    {
    case SubagentStatus::Pending:
        return "pending";
    case SubagentStatus::Active:
        return "active";
    case SubagentStatus::Completed:
        return "completed";
    case SubagentStatus::Errored:
        return "errored";
    case SubagentStatus::Killed:
        return "killed";
    }

    return "pending";
}

SubagentStatus SubagentRunRecord::statusFromString(const std::string &s)
{
    if (s == "active")
        return SubagentStatus::Active;
    if (s == "completed")
        return SubagentStatus::Completed;
    if (s == "errored")
        return SubagentStatus::Errored;
    if (s == "killed")
        return SubagentStatus::Killed;
    return SubagentStatus::Pending;
}

nlohmann::json SubagentRunRecord::toJson() const
{
    nlohmann::json j;
    j["run_id"] = runId;
    j["child_session_key"] = childSessionKey;
    j["requester_session_key"] = requesterSessionKey;
    j["task"] = task;
    j["status"] = statusToString(status);
    j["depth"] = depth;
    j["created_at"] = createdAt;
    j["updated_at"] = updatedAt;

    if (!outcome.empty())
        j["outcome"] = outcome;
    if (!model.empty())
        j["model"] = model;
    if (!thinkingLevel.empty())
        j["thinking_level"] = thinkingLevel;
    if (timeoutSeconds > 0)
        j["timeout_seconds"] = timeoutSeconds;
    if (!lastOutput.empty())
        j["last_output"] = lastOutput;

    return j;
}

SubagentRunRecord SubagentRunRecord::fromJson(const nlohmann::json &j)
{
    SubagentRunRecord r;
    r.runId = j.value("run_id", "");
    r.childSessionKey = j.value("child_session_key", "");
    r.requesterSessionKey = j.value("requester_session_key", "");
    r.task = j.value("task", "");
    r.status = statusFromString(j.value("status", "pending"));
    r.outcome = j.value("outcome", "");
    r.model = j.value("model", "");
    r.thinkingLevel = j.value("thinking_level", "");
    r.depth = j.value("depth", 0);
    r.timeoutSeconds = j.value("timeout_seconds", 0);
    r.lastOutput = j.value("last_output", "");
    r.createdAt = j.value("created_at", "");
    r.updatedAt = j.value("updated_at", "");
    return r;
}

// SubagentRegistry

SubagentRegistry::SubagentRegistry(const std::string &workspacePath)
{
    auto dir = workspacePath + "/.ionclaw";
    std::error_code ec;
    fs::create_directories(dir, ec);
    filePath = dir + "/subagent-runs.json";
}

SubagentRunRecord SubagentRegistry::spawn(
    const std::string &requesterSessionKey,
    const std::string &task,
    const std::string &childSessionKey,
    const std::string &model,
    const std::string &thinkingLevel,
    int parentDepth,
    int timeoutSeconds)
{
    std::lock_guard<std::mutex> lock(mutex);

    SubagentRunRecord record;
    record.runId = util::UniqueId::uuid();
    record.requesterSessionKey = requesterSessionKey;
    record.childSessionKey = childSessionKey;
    record.task = task;
    record.model = model;
    record.thinkingLevel = thinkingLevel;
    record.status = SubagentStatus::Pending;
    record.depth = parentDepth + 1;
    record.timeoutSeconds = timeoutSeconds;
    record.createdAt = util::TimeHelper::now();
    record.updatedAt = record.createdAt;

    records[record.runId] = record;
    sessionToRunId[childSessionKey] = record.runId;

    save();

    spdlog::info("[SubagentRegistry] Spawned run {} (depth {}, child session {})",
                 record.runId, record.depth, childSessionKey);

    return record;
}

void SubagentRegistry::updateStatus(const std::string &runId, SubagentStatus status, const std::string &outcome)
{
    std::lock_guard<std::mutex> lock(mutex);

    auto it = records.find(runId);

    if (it == records.end())
    {
        spdlog::warn("[SubagentRegistry] Run not found: {}", runId);
        return;
    }

    it->second.status = status;
    it->second.updatedAt = util::TimeHelper::now();

    if (!outcome.empty())
    {
        // cap frozen result text to prevent unbounded storage
        if (outcome.size() > MAX_FROZEN_RESULT_BYTES)
        {
            it->second.outcome = util::StringHelper::utf8SafeTruncate(outcome, MAX_FROZEN_RESULT_BYTES) + "\n[result truncated at 100KB]";
            spdlog::info("[SubagentRegistry] Capped frozen result for run {} ({}B -> {}B)",
                         runId, outcome.size(), MAX_FROZEN_RESULT_BYTES);
        }
        else
        {
            it->second.outcome = outcome;
        }
    }

    save();
}

void SubagentRegistry::updateProgress(const std::string &runId, const std::string &output)
{
    static constexpr size_t MAX_PROGRESS_CHARS = 500;

    std::lock_guard<std::mutex> lock(mutex);

    auto it = records.find(runId);

    if (it == records.end())
    {
        return;
    }

    if (output.size() > MAX_PROGRESS_CHARS)
    {
        // UTF-8 safe tail extraction: skip continuation bytes at start of tail
        auto tailStart = output.size() - MAX_PROGRESS_CHARS;

        while (tailStart < output.size() &&
               (static_cast<unsigned char>(output[tailStart]) & 0xC0) == 0x80)
        {
            tailStart++;
        }

        it->second.lastOutput = output.substr(tailStart);
    }
    else
    {
        it->second.lastOutput = output;
    }

    it->second.updatedAt = util::TimeHelper::now();
}

SubagentRunRecord SubagentRegistry::getRecord(const std::string &runId) const
{
    std::lock_guard<std::mutex> lock(mutex);

    auto it = records.find(runId);

    if (it == records.end())
    {
        return {};
    }

    return it->second;
}

std::vector<SubagentRunRecord> SubagentRegistry::getChildren(const std::string &requesterSessionKey) const
{
    std::lock_guard<std::mutex> lock(mutex);

    std::vector<SubagentRunRecord> result;

    for (const auto &[id, record] : records)
    {
        if (record.requesterSessionKey == requesterSessionKey)
        {
            result.push_back(record);
        }
    }

    return result;
}

int SubagentRegistry::getActiveChildCount(const std::string &requesterSessionKey) const
{
    std::lock_guard<std::mutex> lock(mutex);

    int count = 0;

    for (const auto &[id, record] : records)
    {
        if (record.requesterSessionKey == requesterSessionKey &&
            (record.status == SubagentStatus::Pending || record.status == SubagentStatus::Active))
        {
            count++;
        }
    }

    return count;
}

int SubagentRegistry::getDepth(const std::string &sessionKey) const
{
    std::lock_guard<std::mutex> lock(mutex);

    auto it = sessionToRunId.find(sessionKey);

    if (it == sessionToRunId.end())
    {
        return 0;
    }

    auto recIt = records.find(it->second);

    if (recIt == records.end())
    {
        return 0;
    }

    return recIt->second.depth;
}

bool SubagentRegistry::allChildrenTerminal(const std::string &requesterSessionKey) const
{
    std::lock_guard<std::mutex> lock(mutex);

    for (const auto &[id, record] : records)
    {
        if (record.requesterSessionKey == requesterSessionKey &&
            (record.status == SubagentStatus::Pending || record.status == SubagentStatus::Active))
        {
            return false;
        }
    }

    return true;
}

int SubagentRegistry::killRun(const std::string &runId, bool cascade)
{
    std::lock_guard<std::mutex> lock(mutex);
    int killed = 0;

    auto it = records.find(runId);

    if (it == records.end())
    {
        spdlog::warn("[SubagentRegistry] Kill: run not found: {}", runId);
        return 0;
    }

    // only kill active/pending runs
    if (it->second.status == SubagentStatus::Pending || it->second.status == SubagentStatus::Active)
    {
        it->second.status = SubagentStatus::Killed;
        it->second.outcome = "Killed by parent";
        it->second.updatedAt = util::TimeHelper::now();
        killed++;
        spdlog::info("[SubagentRegistry] Killed run {}", runId);
    }

    // cascade: kill all descendants (runs whose requester is this run's child session)
    if (cascade)
    {
        auto childSessionKey = it->second.childSessionKey;

        // collect descendant run ids to kill (BFS through the tree)
        std::vector<std::string> pendingSessions = {childSessionKey};

        while (!pendingSessions.empty())
        {
            auto currentSession = pendingSessions.back();
            pendingSessions.pop_back();

            for (auto &[id, record] : records)
            {
                if (record.requesterSessionKey == currentSession &&
                    (record.status == SubagentStatus::Pending || record.status == SubagentStatus::Active))
                {
                    record.status = SubagentStatus::Killed;
                    record.outcome = "Killed by ancestor cascade";
                    record.updatedAt = util::TimeHelper::now();
                    killed++;
                    pendingSessions.push_back(record.childSessionKey);
                    spdlog::info("[SubagentRegistry] Cascade killed descendant run {}", id);
                }
            }
        }
    }

    if (killed > 0)
    {
        save();
    }

    return killed;
}

void SubagentRegistry::load()
{
    std::lock_guard<std::mutex> lock(mutex);

    records.clear();
    sessionToRunId.clear();

    if (!fs::exists(filePath))
    {
        return;
    }

    std::ifstream ifs(filePath);

    if (!ifs.is_open())
    {
        return;
    }

    try
    {
        auto j = nlohmann::json::parse(ifs);

        if (!j.is_array())
        {
            return;
        }

        for (const auto &item : j)
        {
            auto record = SubagentRunRecord::fromJson(item);

            if (!record.runId.empty())
            {
                records[record.runId] = record;
                sessionToRunId[record.childSessionKey] = record.runId;
            }
        }

        spdlog::info("[SubagentRegistry] Loaded {} subagent runs", records.size());
    }
    catch (const std::exception &e)
    {
        spdlog::warn("[SubagentRegistry] Failed to parse {}: {}", filePath, e.what());
    }
}

void SubagentRegistry::save()
{
    nlohmann::json arr = nlohmann::json::array();

    for (const auto &[id, record] : records)
    {
        arr.push_back(record.toJson());
    }

    std::ofstream ofs(filePath, std::ios::trunc);

    if (!ofs.is_open())
    {
        spdlog::error("[SubagentRegistry] Failed to open {} for writing", filePath);
        return;
    }

    ofs << arr.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
    ofs.flush();
}

int SubagentRegistry::recoverStaleRuns()
{
    std::lock_guard<std::mutex> lock(mutex);
    int recovered = 0;

    for (auto &[id, record] : records)
    {
        if (record.status == SubagentStatus::Pending || record.status == SubagentStatus::Active)
        {
            record.status = SubagentStatus::Errored;
            record.outcome = "Interrupted by server restart";
            record.updatedAt = ionclaw::util::TimeHelper::now();
            recovered++;
        }
    }

    if (recovered > 0)
    {
        save();
        spdlog::info("[SubagentRegistry] Recovered {} stale subagent runs", recovered);
    }

    return recovered;
}

int SubagentRegistry::checkTimeouts()
{
    std::lock_guard<std::mutex> lock(mutex);
    int timedOut = 0;
    auto now = util::TimeHelper::now();

    for (auto &[id, record] : records)
    {
        if (record.timeoutSeconds <= 0)
        {
            continue;
        }

        if (record.status != SubagentStatus::Pending && record.status != SubagentStatus::Active)
        {
            continue;
        }

        auto elapsed = util::TimeHelper::diffSeconds(record.createdAt, now);

        if (elapsed >= record.timeoutSeconds)
        {
            record.status = SubagentStatus::Killed;
            record.outcome = "Timed out after " + std::to_string(record.timeoutSeconds) + " seconds";
            record.updatedAt = now;
            timedOut++;
            spdlog::warn("[SubagentRegistry] Run {} timed out after {}s", id, elapsed);
        }
    }

    if (timedOut > 0)
    {
        save();
    }

    return timedOut;
}

} // namespace agent
} // namespace ionclaw
