#pragma once

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "ionclaw/bus/EventDispatcher.hpp"
#include "ionclaw/task/Task.hpp"
#include "nlohmann/json.hpp"

namespace ionclaw
{
namespace task
{

class TaskManager
{
public:
    TaskManager(const std::string &tasksFilePath, std::shared_ptr<ionclaw::bus::EventDispatcher> dispatcher);

    Task createTask(const std::string &title, const std::string &description,
                    const std::string &channel, const std::string &chatId,
                    const std::string &parentTaskId = "");

    void updateState(const std::string &taskId, TaskState state, const std::string &result = "");
    void setAgent(const std::string &taskId, const std::string &agentName);
    void incrementIteration(const std::string &taskId);
    void incrementToolCount(const std::string &taskId);
    void setUsage(const std::string &taskId, const nlohmann::json &usage);
    void setLiveState(const std::string &taskId, const nlohmann::json &liveState);
    void setError(const std::string &taskId, const std::string &error);

    Task getTask(const std::string &taskId) const;
    std::vector<Task> listTasks(const std::string &agentFilter = "") const;

    void load();
    void save();
    void recoverStaleTasks();

private:
    std::string tasksFilePath;
    std::map<std::string, Task> tasks;
    mutable std::mutex mutex;
    std::mutex fileMutex; // protects file I/O (appendToFile, save, recoverStaleTasks)
    std::shared_ptr<ionclaw::bus::EventDispatcher> dispatcher;

    // parse an ISO 8601 timestamp into a time_point
    static std::chrono::system_clock::time_point parseTimestamp(const std::string &str);

    void broadcastUpdate(const Task &task);
    void appendToFile(const Task &task);
};

} // namespace task
} // namespace ionclaw
