#pragma once

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace ionclaw
{
namespace agent
{

enum class SubagentStatus
{
    Pending,
    Active,
    Completed,
    Errored,
    Killed
};

struct SubagentRunRecord
{
    std::string runId;
    std::string childSessionKey;
    std::string requesterSessionKey;
    std::string task;
    SubagentStatus status = SubagentStatus::Pending;
    std::string outcome;
    std::string model;
    std::string thinkingLevel;
    int depth = 0;
    int timeoutSeconds = 0; // 0 = no timeout
    std::string lastOutput; // latest content snippet for progress visibility
    std::string createdAt;
    std::string updatedAt;

    nlohmann::json toJson() const;
    static SubagentRunRecord fromJson(const nlohmann::json &j);
    static std::string statusToString(SubagentStatus s);
    static SubagentStatus statusFromString(const std::string &s);
};

class SubagentRegistry
{
public:
    explicit SubagentRegistry(const std::string &workspacePath);

    static constexpr int MAX_DEPTH = 5;
    static constexpr int MAX_CHILDREN = 5;
    static constexpr size_t MAX_FROZEN_RESULT_BYTES = 100 * 1024;

    static constexpr int DEFAULT_TIMEOUT_SECONDS = 300;

    SubagentRunRecord spawn(
        const std::string &requesterSessionKey,
        const std::string &task,
        const std::string &childSessionKey,
        const std::string &model = "",
        const std::string &thinkingLevel = "",
        int parentDepth = 0,
        int timeoutSeconds = DEFAULT_TIMEOUT_SECONDS);

    void updateStatus(const std::string &runId, SubagentStatus status, const std::string &outcome = "");
    void updateProgress(const std::string &runId, const std::string &output);
    SubagentRunRecord getRecord(const std::string &runId) const;
    std::vector<SubagentRunRecord> getChildren(const std::string &requesterSessionKey) const;
    int getActiveChildCount(const std::string &requesterSessionKey) const;
    int getDepth(const std::string &sessionKey) const;
    bool allChildrenTerminal(const std::string &requesterSessionKey) const;

    // kill a run and optionally cascade to all descendants
    int killRun(const std::string &runId, bool cascade = true);

    void load();
    void save();
    int recoverStaleRuns();
    int checkTimeouts();

private:
    std::string filePath;
    std::map<std::string, SubagentRunRecord> records;
    std::map<std::string, std::string> sessionToRunId;
    mutable std::mutex mutex;
};

} // namespace agent
} // namespace ionclaw
