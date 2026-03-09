#pragma once

#include <cstdint>
#include <string>

namespace ionclaw
{
namespace cron
{

struct CronSchedule
{
    std::string kind = "every"; // "at", "every", "cron"
    int64_t atMs = 0;
    int64_t everyMs = 0;
    std::string expr;
    std::string tz;
};

struct CronPayload
{
    std::string message;
    bool deliver = true;
    std::string channel;
    std::string to;
};

struct CronJobState
{
    int64_t nextRunMs = 0;
    int64_t lastRunMs = 0;
    std::string status = "pending";
    int runCount = 0;
    int errors = 0;
};

struct CronJob
{
    std::string id;
    std::string name;
    CronSchedule schedule;
    CronPayload payload;
    CronJobState state;
    bool deleteAfterRun = false;
};

} // namespace cron
} // namespace ionclaw
