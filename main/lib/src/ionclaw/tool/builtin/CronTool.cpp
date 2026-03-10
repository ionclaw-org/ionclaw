#include "ionclaw/tool/builtin/CronTool.hpp"

#include <iomanip>
#include <sstream>

#include "ionclaw/cron/CronParser.hpp"
#include "ionclaw/cron/CronService.hpp"
#include "ionclaw/cron/CronTypes.hpp"
#include "ionclaw/util/StringHelper.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

ToolResult CronTool::execute(const nlohmann::json &params, const ToolContext &context)
{
    auto action = params.at("action").get<std::string>();

    if (!context.cronService)
    {
        return "Error: cron service not available";
    }

    if (action == "add")
    {
        if (!params.contains("message") || !params["message"].is_string())
        {
            return "Error: 'message' is required for add action";
        }

        auto message = params["message"].get<std::string>();

        // parse channel from session key
        std::string channel = "web";
        std::string to = context.sessionKey;

        auto colonPos = context.sessionKey.find(':');

        if (colonPos != std::string::npos)
        {
            channel = context.sessionKey.substr(0, colonPos);
            to = context.sessionKey.substr(colonPos + 1);
        }

        if (channel.empty() || to.empty())
        {
            return "Error: no session context (channel/chat_id)";
        }

        // determine schedule
        ionclaw::cron::CronSchedule schedule;
        bool deleteAfterRun = false;

        auto tz = params.contains("tz") && params["tz"].is_string()
                      ? params["tz"].get<std::string>()
                      : "";

        if (!tz.empty() && !params.contains("cron_expr"))
        {
            return "Error: tz can only be used with cron_expr";
        }

        if (!tz.empty() && !ionclaw::cron::CronParser::isValidTimezone(tz))
        {
            return "Error: unknown timezone '" + tz + "'";
        }

        if (params.contains("every_seconds") && params["every_seconds"].is_number_integer())
        {
            auto seconds = params["every_seconds"].get<int>();

            constexpr int minSeconds = ionclaw::cron::CronService::TICK_INTERVAL_MS / 1000;

            if (seconds < minSeconds)
            {
                return "Error: every_seconds must be at least " + std::to_string(minSeconds);
            }

            schedule.kind = "every";
            schedule.everyMs = static_cast<int64_t>(seconds) * 1000;
        }
        else if (params.contains("cron_expr") && params["cron_expr"].is_string())
        {
            schedule.kind = "cron";
            schedule.expr = params["cron_expr"].get<std::string>();
            schedule.tz = tz;
        }
        else if (params.contains("at") && params["at"].is_string())
        {
            auto atStr = params["at"].get<std::string>();

            // parse ISO datetime to epoch ms
            std::tm tm{};
            std::istringstream ss(atStr);
            ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");

            if (ss.fail())
            {
                return "Error: invalid datetime format, use ISO 8601 (e.g. 2026-03-05T15:30:00)";
            }

            auto epochTime = std::mktime(&tm);
            schedule.kind = "at";
            schedule.atMs = static_cast<int64_t>(epochTime) * 1000;
            deleteAfterRun = true;
        }
        else
        {
            return "Error: one of 'every_seconds', 'cron_expr', or 'at' is required";
        }

        auto job = context.cronService->addJob(
            ionclaw::util::StringHelper::utf8SafeTruncate(message, 30),
            schedule,
            message,
            true,
            channel,
            to,
            deleteAfterRun);

        return "Created job '" + job.name + "' (id: " + job.id + ")";
    }
    else if (action == "list")
    {
        auto jobs = context.cronService->listJobs();

        if (jobs.empty())
        {
            return "No scheduled jobs.";
        }

        std::ostringstream output;
        output << "Scheduled jobs:\n";

        for (const auto &job : jobs)
        {
            output << "- " << job.name << " (id: " << job.id << ", " << job.schedule.kind << ")\n";
        }

        return output.str();
    }
    else if (action == "remove")
    {
        if (!params.contains("job_id") || !params["job_id"].is_string())
        {
            return "Error: 'job_id' is required for remove action";
        }

        auto jobId = params["job_id"].get<std::string>();

        if (context.cronService->removeJob(jobId))
        {
            return "Removed job " + jobId;
        }

        return "Job " + jobId + " not found";
    }

    return "Error: action must be 'add', 'list', or 'remove'";
}

ToolSchema CronTool::schema() const
{
    return {
        "cron",
        "Schedule reminders and recurring tasks. Actions: add, list, remove.",
        {{"type", "object"},
         {"properties",
          {{"action", {{"type", "string"}, {"enum", nlohmann::json::array({"add", "list", "remove"})}, {"description", "Action to perform"}}},
           {"message", {{"type", "string"}, {"description", "Reminder message (for add)"}}},
           {"every_seconds", {{"type", "integer"}, {"description", "Interval in seconds (for recurring tasks)"}}},
           {"cron_expr", {{"type", "string"}, {"description", "Cron expression like '0 9 * * *'"}}},
           {"tz", {{"type", "string"}, {"description", "IANA timezone for cron expressions"}}},
           {"at", {{"type", "string"}, {"description", "ISO datetime for one-time execution"}}},
           {"job_id", {{"type", "string"}, {"description", "Job ID (for remove)"}}}}},
         {"required", nlohmann::json::array({"action"})}}};
}

std::set<std::string> CronTool::supportedPlatforms() const
{
    return {"linux", "macos", "windows"};
}

} // namespace builtin
} // namespace tool
} // namespace ionclaw
