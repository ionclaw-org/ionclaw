#pragma once

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
};

} // namespace task
} // namespace ionclaw
