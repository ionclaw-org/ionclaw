#include "ionclaw/cron/CronService.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "spdlog/spdlog.h"

#include "ionclaw/bus/Events.hpp"
#include "ionclaw/cron/CronParser.hpp"
#include "ionclaw/util/StringHelper.hpp"
#include "ionclaw/util/TimeHelper.hpp"
#include "ionclaw/util/UniqueId.hpp"

namespace ionclaw
{
namespace cron
{

CronService::CronService(
    std::shared_ptr<ionclaw::bus::MessageBus> bus,
    std::shared_ptr<ionclaw::task::TaskManager> taskManager,
    const std::string &workspacePath)
    : bus(std::move(bus))
    , taskManager(std::move(taskManager))
    , storePath(workspacePath + "/cron_jobs.json")
{
    load();
}

void CronService::start()
{
    if (running.exchange(true))
    {
        return;
    }

    size_t jobCount;
    {
        std::lock_guard<std::mutex> lock(mutex);
        jobCount = jobs.size();
    }
    loopThread = std::thread(&CronService::runLoop, this);
    spdlog::info("[CronService] started ({} jobs)", jobCount);
}

void CronService::stop()
{
    if (!running.exchange(false))
    {
        return;
    }

    if (loopThread.joinable())
    {
        loopThread.join();
    }

    spdlog::info("[CronService] stopped");
}

CronJob CronService::addJob(
    const std::string &name,
    const CronSchedule &schedule,
    const std::string &message,
    bool deliver,
    const std::string &channel,
    const std::string &to,
    bool deleteAfterRun)
{
    CronJob job;
    job.id = ionclaw::util::UniqueId::shortId();
    job.name = name;
    job.schedule = schedule;
    job.payload.message = message;
    job.payload.deliver = deliver;
    job.payload.channel = channel;
    job.payload.to = to;
    job.state.nextRunMs = computeNextRunMs(schedule);
    job.state.status = "active";
    job.deleteAfterRun = deleteAfterRun;

    {
        std::lock_guard<std::mutex> lock(mutex);
        jobs.push_back(job);
        persist();
    }

    spdlog::info("[CronService] job added: {} (id: {})", name, job.id);
    return job;
}

bool CronService::removeJob(const std::string &jobId)
{
    std::lock_guard<std::mutex> lock(mutex);

    auto it = std::remove_if(jobs.begin(), jobs.end(),
                             [&jobId](const CronJob &j)
                             { return j.id == jobId; });

    if (it == jobs.end())
    {
        return false;
    }

    jobs.erase(it, jobs.end());
    persist();
    spdlog::info("[CronService] job removed: {}", jobId);
    return true;
}

std::vector<CronJob> CronService::listJobs() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return jobs;
}

// background loop

void CronService::runLoop()
{
    while (running.load())
    {
        try
        {
            // sleep in small increments for fast shutdown
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(TICK_INTERVAL_MS);

            while (running.load() && std::chrono::steady_clock::now() < deadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }

            if (!running.load())
            {
                break;
            }

            tick();
        }
        catch (const std::exception &e)
        {
            spdlog::error("[CronService] tick error: {}", e.what());
        }
        catch (...)
        {
            spdlog::error("[CronService] non-standard exception in run loop");
        }
    }
}

void CronService::tick()
{
    auto nowMs = ionclaw::util::TimeHelper::epochMs();

    std::lock_guard<std::mutex> lock(mutex);

    std::vector<std::string> toDelete;
    bool dirty = false;

    for (auto &job : jobs)
    {
        if (job.state.status != "active")
        {
            continue;
        }

        if (job.state.nextRunMs > nowMs)
        {
            continue;
        }

        spdlog::info("[CronService] executing job: {} (id: {})", job.name, job.id);

        // publish inbound message to trigger agent processing
        try
        {
            // route through the originating channel so the response reaches the right place
            auto effectiveChannel = job.payload.channel.empty() ? "web" : job.payload.channel;
            auto effectiveChatId = job.payload.to.empty()
                                       ? "cron_" + job.id + "_" + ionclaw::util::UniqueId::shortId()
                                       : job.payload.to;

            // create task for board tracking
            std::string taskId;

            if (taskManager)
            {
                auto taskTitle = "[cron] " + ionclaw::util::StringHelper::utf8SafeTruncate(job.name, 80);
                auto task = taskManager->createTask(taskTitle, job.payload.message, effectiveChannel, effectiveChatId);
                taskId = task.id;
            }

            ionclaw::bus::InboundMessage msg;
            msg.channel = effectiveChannel;
            msg.senderId = "cron";
            msg.chatId = effectiveChatId;
            msg.content = job.payload.message;
            msg.metadata = {{"cron_job_id", job.id}, {"cron_job_name", job.name}};

            if (!taskId.empty())
            {
                msg.metadata["task_id"] = taskId;
            }

            bus->publishInbound(msg);

            job.state.lastRunMs = nowMs;
            job.state.runCount++;
            dirty = true;

            // handle one-shot jobs
            if (job.deleteAfterRun || job.schedule.kind == "at")
            {
                toDelete.push_back(job.id);
            }
            else
            {
                job.state.nextRunMs = computeNextRunMs(job.schedule);
            }
        }
        catch (const std::exception &e)
        {
            spdlog::error("[CronService] job {} failed: {}", job.id, e.what());
            job.state.errors++;
            dirty = true;
        }
    }

    // remove completed one-shot jobs
    for (const auto &id : toDelete)
    {
        jobs.erase(
            std::remove_if(jobs.begin(), jobs.end(),
                           [&id](const CronJob &j)
                           { return j.id == id; }),
            jobs.end());
    }

    if (dirty || !toDelete.empty())
    {
        persist();
    }
}

// persistence

void CronService::load()
{
    if (!std::filesystem::exists(storePath))
    {
        return;
    }

    try
    {
        std::ifstream file(storePath);

        if (!file.is_open())
        {
            return;
        }

        auto data = nlohmann::json::parse(file);

        for (const auto &jd : data.value("jobs", nlohmann::json::array()))
        {
            jobs.push_back(jobFromJson(jd));
        }

        spdlog::info("[CronService] loaded {} jobs from {}", jobs.size(), storePath);
    }
    catch (const std::exception &e)
    {
        spdlog::warn("[CronService] failed to load jobs: {}", e.what());
    }
}

void CronService::persist()
{
    try
    {
        // ensure parent directory exists
        auto parentDir = std::filesystem::path(storePath).parent_path();

        if (!std::filesystem::exists(parentDir))
        {
            std::error_code ec;
            std::filesystem::create_directories(parentDir, ec);

            if (ec)
            {
                spdlog::error("[CronService] Could not create directory '{}': {}", parentDir.string(), ec.message());
                return;
            }
        }

        nlohmann::json data;
        data["version"] = 1;
        data["jobs"] = nlohmann::json::array();

        for (const auto &job : jobs)
        {
            data["jobs"].push_back(jobToJson(job));
        }

        std::ofstream file(storePath);

        if (!file.is_open())
        {
            spdlog::error("[CronService] failed to open file for writing: {}", storePath);
            return;
        }

        file << data.dump(2);

        if (!file.good())
        {
            spdlog::error("[CronService] failed to write to file: {}", storePath);
        }

        file.close();
    }
    catch (const std::exception &e)
    {
        spdlog::error("[CronService] failed to persist jobs: {}", e.what());
    }
}

int64_t CronService::computeNextRunMs(const CronSchedule &schedule)
{
    auto nowMs = ionclaw::util::TimeHelper::epochMs();

    if (schedule.kind == "at")
    {
        return schedule.atMs;
    }

    if (schedule.kind == "every")
    {
        return nowMs + schedule.everyMs;
    }

    if (schedule.kind == "cron")
    {
        return CronParser::nextRun(schedule.expr, schedule.tz);
    }

    // fallback: 1 minute from now
    return nowMs + 60000;
}

nlohmann::json CronService::jobToJson(const CronJob &job)
{
    return {
        {"id", job.id},
        {"name", job.name},
        {"schedule",
         {{"kind", job.schedule.kind},
          {"at_ms", job.schedule.atMs},
          {"every_ms", job.schedule.everyMs},
          {"expr", job.schedule.expr},
          {"tz", job.schedule.tz}}},
        {"payload",
         {{"message", job.payload.message},
          {"deliver", job.payload.deliver},
          {"channel", job.payload.channel},
          {"to", job.payload.to}}},
        {"state",
         {{"next_run_ms", job.state.nextRunMs},
          {"last_run_ms", job.state.lastRunMs},
          {"status", job.state.status},
          {"run_count", job.state.runCount},
          {"errors", job.state.errors}}},
        {"delete_after_run", job.deleteAfterRun},
    };
}

CronJob CronService::jobFromJson(const nlohmann::json &j)
{
    CronJob job;
    job.id = j.value("id", "");
    job.name = j.value("name", "");
    job.deleteAfterRun = j.value("delete_after_run", false);

    if (j.contains("schedule"))
    {
        auto &s = j["schedule"];
        job.schedule.kind = s.value("kind", "every");
        job.schedule.atMs = s.value("at_ms", int64_t(0));
        job.schedule.everyMs = s.value("every_ms", int64_t(0));
        job.schedule.expr = s.value("expr", "");
        job.schedule.tz = s.value("tz", "");
    }

    if (j.contains("payload"))
    {
        auto &p = j["payload"];
        job.payload.message = p.value("message", "");
        job.payload.deliver = p.value("deliver", true);
        job.payload.channel = p.value("channel", "");
        job.payload.to = p.value("to", "");
    }

    if (j.contains("state"))
    {
        auto &st = j["state"];
        job.state.nextRunMs = st.value("next_run_ms", int64_t(0));
        job.state.lastRunMs = st.value("last_run_ms", int64_t(0));
        job.state.status = st.value("status", "pending");
        job.state.runCount = st.value("run_count", 0);
        job.state.errors = st.value("errors", 0);
    }

    return job;
}

} // namespace cron
} // namespace ionclaw
