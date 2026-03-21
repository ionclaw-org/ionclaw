#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "nlohmann/json.hpp"

namespace ionclaw
{
namespace task
{

enum class TaskState
{
    Todo,
    Doing,
    Done,
    Error
};

struct Task
{
    std::string id;
    std::string title;
    std::string description;
    TaskState state = TaskState::Todo;
    std::string channel;
    std::string chatId;
    std::string agentName;
    std::string createdAt;
    std::string updatedAt;
    std::string completedAt;
    std::string result;
    std::string errorMessage;
    int iterationCount = 0;
    int toolCount = 0;
    nlohmann::json usage;
    nlohmann::json liveState;
    std::string parentTaskId;

    nlohmann::json toJson() const;
    static Task fromJson(const nlohmann::json &j);
    std::string sessionKey() const;
    int64_t durationSeconds() const;

    static std::string stateToString(TaskState state);
    static TaskState stateFromString(const std::string &str);

private:
    // json deserialization helpers
    static std::string jsonString(const nlohmann::json &j, const std::string &key);
    static int jsonInt(const nlohmann::json &j, const std::string &key);
    static std::chrono::system_clock::time_point parseIso8601(const std::string &str);
};

} // namespace task
} // namespace ionclaw
