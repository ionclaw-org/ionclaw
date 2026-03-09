#include "ionclaw/task/TaskManager.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "ionclaw/util/TimeHelper.hpp"
#include "ionclaw/util/UniqueId.hpp"
#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace task
{

namespace fs = std::filesystem;

TaskManager::TaskManager(const std::string &tasksFilePath, std::shared_ptr<ionclaw::bus::EventDispatcher> dispatcher)
    : tasksFilePath(tasksFilePath)
    , dispatcher(std::move(dispatcher))
{
    std::error_code ec;
    auto parentDir = fs::path(tasksFilePath).parent_path();

    if (!parentDir.empty())
    {
        fs::create_directories(parentDir, ec);

        if (ec)
        {
            spdlog::error("Failed to create tasks directory {}: {}", parentDir.string(), ec.message());
        }
    }
}

Task TaskManager::createTask(const std::string &title, const std::string &description,
                             const std::string &channel, const std::string &chatId,
                             const std::string &parentTaskId)
{
    Task snapshot;

    {
        std::lock_guard<std::mutex> lock(mutex);

        Task task;
        task.id = util::UniqueId::uuid();
        task.title = title;
        task.description = description;
        task.state = TaskState::Todo;
        task.channel = channel;
        task.chatId = chatId;
        task.createdAt = util::TimeHelper::now();
        task.updatedAt = task.createdAt;
        task.parentTaskId = parentTaskId;

        tasks[task.id] = task;
        snapshot = task;
    }

    appendToFile(snapshot);
    broadcastUpdate(snapshot);

    spdlog::info("Task created: {}", snapshot.id);

    return snapshot;
}

void TaskManager::updateState(const std::string &taskId, TaskState state, const std::string &result)
{
    Task snapshot;

    {
        std::lock_guard<std::mutex> lock(mutex);

        auto it = tasks.find(taskId);

        if (it == tasks.end())
        {
            spdlog::warn("Task not found for updateState: {}", taskId);
            return;
        }

        auto &task = it->second;
        task.state = state;
        task.updatedAt = util::TimeHelper::now();

        if (!result.empty())
        {
            task.result = result;
        }

        if (state == TaskState::Done || state == TaskState::Error)
        {
            task.completedAt = task.updatedAt;
        }

        snapshot = task;
    }

    appendToFile(snapshot);
    broadcastUpdate(snapshot);
}

void TaskManager::setAgent(const std::string &taskId, const std::string &agentName)
{
    Task snapshot;

    {
        std::lock_guard<std::mutex> lock(mutex);

        auto it = tasks.find(taskId);

        if (it == tasks.end())
        {
            spdlog::warn("Task not found for setAgent: {}", taskId);
            return;
        }

        it->second.agentName = agentName;
        it->second.updatedAt = util::TimeHelper::now();
        snapshot = it->second;
    }

    appendToFile(snapshot);
    broadcastUpdate(snapshot);
}

void TaskManager::incrementIteration(const std::string &taskId)
{
    Task snapshot;

    {
        std::lock_guard<std::mutex> lock(mutex);

        auto it = tasks.find(taskId);

        if (it == tasks.end())
        {
            spdlog::warn("Task not found for incrementIteration: {}", taskId);
            return;
        }

        it->second.iterationCount++;
        it->second.updatedAt = util::TimeHelper::now();
        snapshot = it->second;
    }

    appendToFile(snapshot);
    broadcastUpdate(snapshot);
}

void TaskManager::incrementToolCount(const std::string &taskId)
{
    Task snapshot;

    {
        std::lock_guard<std::mutex> lock(mutex);

        auto it = tasks.find(taskId);

        if (it == tasks.end())
        {
            spdlog::warn("Task not found for incrementToolCount: {}", taskId);
            return;
        }

        it->second.toolCount++;
        it->second.updatedAt = util::TimeHelper::now();
        snapshot = it->second;
    }

    appendToFile(snapshot);
    broadcastUpdate(snapshot);
}

void TaskManager::setUsage(const std::string &taskId, const nlohmann::json &usage)
{
    Task snapshot;

    {
        std::lock_guard<std::mutex> lock(mutex);

        auto it = tasks.find(taskId);

        if (it == tasks.end())
        {
            spdlog::warn("Task not found for setUsage: {}", taskId);
            return;
        }

        it->second.usage = usage;
        it->second.updatedAt = util::TimeHelper::now();
        snapshot = it->second;
    }

    appendToFile(snapshot);
    broadcastUpdate(snapshot);
}

void TaskManager::setLiveState(const std::string &taskId, const nlohmann::json &liveState)
{
    Task snapshot;

    {
        std::lock_guard<std::mutex> lock(mutex);

        auto it = tasks.find(taskId);

        if (it == tasks.end())
        {
            spdlog::warn("Task not found for setLiveState: {}", taskId);
            return;
        }

        it->second.liveState = liveState;
        it->second.updatedAt = util::TimeHelper::now();
        snapshot = it->second;
    }

    appendToFile(snapshot);
    broadcastUpdate(snapshot);
}

void TaskManager::setError(const std::string &taskId, const std::string &error)
{
    Task snapshot;

    {
        std::lock_guard<std::mutex> lock(mutex);

        auto it = tasks.find(taskId);

        if (it == tasks.end())
        {
            spdlog::warn("Task not found for setError: {}", taskId);
            return;
        }

        it->second.errorMessage = error;
        it->second.state = TaskState::Error;
        it->second.completedAt = util::TimeHelper::now();
        it->second.updatedAt = it->second.completedAt;
        snapshot = it->second;
    }

    appendToFile(snapshot);
    broadcastUpdate(snapshot);
}

Task TaskManager::getTask(const std::string &taskId) const
{
    std::lock_guard<std::mutex> lock(mutex);

    auto it = tasks.find(taskId);

    if (it == tasks.end())
    {
        spdlog::warn("Task not found: {}", taskId);
        return {};
    }

    return it->second;
}

// parse an ISO 8601 timestamp into a time_point
std::chrono::system_clock::time_point TaskManager::parseTimestamp(const std::string &str)
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

std::vector<Task> TaskManager::listTasks(const std::string &agentFilter) const
{
    std::lock_guard<std::mutex> lock(mutex);

    auto now = std::chrono::system_clock::now();
    auto cutoff = now - std::chrono::hours(24);

    std::vector<Task> result;

    for (const auto &pair : tasks)
    {
        const auto &task = pair.second;

        // filter by agent if provided
        if (!agentFilter.empty() && task.agentName != agentFilter)
        {
            continue;
        }

        // filter out stale tasks (older than 24h in terminal/idle states)
        if (task.state != TaskState::Doing && !task.updatedAt.empty())
        {
            auto updatedTime = parseTimestamp(task.updatedAt);

            if (updatedTime < cutoff)
            {
                continue;
            }
        }

        result.push_back(task);
    }

    return result;
}

void TaskManager::load()
{
    std::lock_guard<std::mutex> lock(mutex);

    if (!fs::exists(tasksFilePath))
    {
        return;
    }

    std::ifstream ifs(tasksFilePath);

    if (!ifs.is_open())
    {
        spdlog::error("Failed to open tasks file: {}", tasksFilePath);
        return;
    }

    tasks.clear();
    std::string line;

    while (std::getline(ifs, line))
    {
        if (line.empty())
        {
            continue;
        }

        try
        {
            auto j = nlohmann::json::parse(line);
            auto task = Task::fromJson(j);

            if (!task.id.empty())
            {
                tasks[task.id] = std::move(task);
            }
        }
        catch (const nlohmann::json::exception &e)
        {
            spdlog::warn("Failed to parse task line: {}", e.what());
        }
    }

    spdlog::info("Loaded {} tasks from {}", tasks.size(), tasksFilePath);
}

void TaskManager::save()
{
    std::lock_guard<std::mutex> lock(mutex);
    std::lock_guard<std::mutex> flock(fileMutex);

    std::ofstream ofs(tasksFilePath, std::ios::trunc);

    if (!ofs.is_open())
    {
        spdlog::error("Failed to open tasks file for save: {}", tasksFilePath);
        return;
    }

    for (const auto &pair : tasks)
    {
        ofs << pair.second.toJson().dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << "\n";
    }

    ofs.flush();
}

void TaskManager::recoverStaleTasks()
{
    std::vector<Task> snapshots;
    int recovered = 0;

    {
        std::lock_guard<std::mutex> lock(mutex);

        for (auto &[id, task] : tasks)
        {
            if (task.state == TaskState::Doing)
            {
                task.state = TaskState::Error;
                task.errorMessage = "Interrupted by server restart";
                task.completedAt = util::TimeHelper::now();
                task.updatedAt = task.completedAt;
                recovered++;
            }
        }

        if (recovered > 0)
        {
            snapshots.reserve(tasks.size());

            for (const auto &[id, t] : tasks)
            {
                snapshots.push_back(t);
            }
        }
    }

    // write file outside data mutex
    if (recovered > 0)
    {
        std::lock_guard<std::mutex> flock(fileMutex);
        std::ofstream ofs(tasksFilePath, std::ios::trunc);

        if (ofs.is_open())
        {
            for (const auto &t : snapshots)
            {
                ofs << t.toJson().dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << "\n";
            }

            ofs.flush();
        }

        spdlog::info("[TaskManager] Recovered {} stale DOING tasks to ERROR", recovered);
    }
}

void TaskManager::appendToFile(const Task &task)
{
    std::lock_guard<std::mutex> flock(fileMutex);

    std::ofstream ofs(tasksFilePath, std::ios::app);

    if (!ofs.is_open())
    {
        spdlog::error("Failed to open tasks file for append: {}", tasksFilePath);
        return;
    }

    ofs << task.toJson().dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << "\n";
    ofs.flush();
}

void TaskManager::broadcastUpdate(const Task &task)
{
    if (!dispatcher)
    {
        return;
    }

    std::string eventType = (task.state == TaskState::Todo && task.iterationCount == 0)
                                ? "task:created"
                                : "task:updated";

    dispatcher->broadcast(eventType, task.toJson());
}

} // namespace task
} // namespace ionclaw
