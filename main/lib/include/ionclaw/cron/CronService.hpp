#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "nlohmann/json.hpp"

#include "ionclaw/bus/MessageBus.hpp"
#include "ionclaw/cron/CronTypes.hpp"
#include "ionclaw/task/TaskManager.hpp"

namespace ionclaw
{
namespace cron
{

class CronService
{
public:
    CronService(
        std::shared_ptr<ionclaw::bus::MessageBus> bus,
        std::shared_ptr<ionclaw::task::TaskManager> taskManager,
        const std::string &workspacePath);

    void start();
    void stop();

    CronJob addJob(
        const std::string &name,
        const CronSchedule &schedule,
        const std::string &message,
        bool deliver,
        const std::string &channel,
        const std::string &to,
        bool deleteAfterRun);

    bool removeJob(const std::string &jobId);
    std::vector<CronJob> listJobs() const;

    static constexpr int TICK_INTERVAL_MS = 5000;

private:
    void runLoop();
    void tick();
    void load();
    void persist();

    static int64_t computeNextRunMs(const CronSchedule &schedule);
    static nlohmann::json jobToJson(const CronJob &job);
    static CronJob jobFromJson(const nlohmann::json &j);

    std::shared_ptr<ionclaw::bus::MessageBus> bus;
    std::shared_ptr<ionclaw::task::TaskManager> taskManager;
    std::string storePath;
    std::vector<CronJob> jobs;
    mutable std::mutex mutex;
    std::atomic<bool> running{false};
    std::thread loopThread;
};

} // namespace cron
} // namespace ionclaw
