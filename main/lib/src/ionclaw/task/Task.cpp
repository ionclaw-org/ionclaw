#include "ionclaw/task/Task.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#include "ionclaw/util/StringHelper.hpp"
#include "ionclaw/util/TimeHelper.hpp"

namespace ionclaw
{
namespace task
{

std::string Task::jsonString(const nlohmann::json &j, const std::string &key)
{
    auto it = j.find(key);

    if (it == j.end() || it->is_null())
    {
        return "";
    }

    return it->get<std::string>();
}

int Task::jsonInt(const nlohmann::json &j, const std::string &key)
{
    auto it = j.find(key);

    if (it == j.end() || it->is_null())
    {
        return 0;
    }

    return it->get<int>();
}

std::chrono::system_clock::time_point Task::parseIso8601(const std::string &str)
{
    std::tm tm{};
    std::istringstream iss(str);
    iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");

    if (iss.fail())
    {
        return std::chrono::system_clock::now();
    }

#if defined(_WIN32)
    auto time = _mkgmtime(&tm);
#else
    auto time = timegm(&tm);
#endif

    return std::chrono::system_clock::from_time_t(time);
}

std::string Task::stateToString(TaskState state)
{
    switch (state)
    {
    case TaskState::Todo:
        return "TODO";
    case TaskState::Doing:
        return "DOING";
    case TaskState::Done:
        return "DONE";
    case TaskState::Error:
        return "ERROR";
    default:
        return "TODO";
    }
}

TaskState Task::stateFromString(const std::string &str)
{
    std::string lower = str;
    ionclaw::util::StringHelper::toLowerInPlace(lower);

    if (lower == "doing")
    {
        return TaskState::Doing;
    }

    if (lower == "done")
    {
        return TaskState::Done;
    }

    if (lower == "error")
    {
        return TaskState::Error;
    }

    if (lower == "todo")
    {
        return TaskState::Todo;
    }

    throw std::invalid_argument("Invalid task state: " + str);
}

nlohmann::json Task::toJson() const
{
    nlohmann::json j;
    j["id"] = id;
    j["title"] = title;
    j["description"] = description;
    j["state"] = Task::stateToString(state);
    j["channel"] = channel;
    j["chat_id"] = chatId;
    j["agent_name"] = agentName;
    j["created_at"] = createdAt;
    j["updated_at"] = updatedAt;
    j["completed_at"] = completedAt;
    j["result"] = result;
    j["error_message"] = errorMessage;
    j["iteration_count"] = iterationCount;
    j["tool_count"] = toolCount;
    j["parent_task_id"] = parentTaskId;

    if (!usage.is_null())
    {
        j["usage"] = usage;
    }

    if (!liveState.is_null())
    {
        j["live_state"] = liveState;
    }

    int64_t secs = durationSeconds();
    j["duration_ms"] = (state == TaskState::Done || state == TaskState::Error) ? nlohmann::json(secs * 1000) : nlohmann::json();

    return j;
}

Task Task::fromJson(const nlohmann::json &j)
{
    Task t;
    t.id = jsonString(j, "id");
    t.title = jsonString(j, "title");
    t.description = jsonString(j, "description");
    try
    {
        t.state = Task::stateFromString(jsonString(j, "state"));
    }
    catch (const std::invalid_argument &)
    {
        t.state = TaskState::Todo;
    }
    t.channel = jsonString(j, "channel");
    t.chatId = jsonString(j, "chat_id");
    t.agentName = jsonString(j, "agent_name");
    t.createdAt = jsonString(j, "created_at");
    t.updatedAt = jsonString(j, "updated_at");
    t.completedAt = jsonString(j, "completed_at");
    t.result = jsonString(j, "result");
    t.errorMessage = jsonString(j, "error_message");
    t.iterationCount = jsonInt(j, "iteration_count");
    t.toolCount = jsonInt(j, "tool_count");
    t.parentTaskId = jsonString(j, "parent_task_id");

    if (j.contains("usage"))
    {
        t.usage = j["usage"];
    }

    if (j.contains("live_state"))
    {
        t.liveState = j["live_state"];
    }

    return t;
}

std::string Task::sessionKey() const
{
    return channel + ":" + chatId;
}

int64_t Task::durationSeconds() const
{
    if (createdAt.empty())
    {
        return 0;
    }

    auto start = parseIso8601(createdAt);
    auto end = completedAt.empty() ? std::chrono::system_clock::now() : parseIso8601(completedAt);

    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
    return duration.count();
}

} // namespace task
} // namespace ionclaw
