#include "ionclaw/tool/builtin/CronTool.hpp"

#include <ctime>
#include <iomanip>
#include <locale>
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
            tm.tm_isdst = -1; // let mktime auto-detect DST
            std::istringstream ss(atStr);
            ss.imbue(std::locale::classic());
            ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");

            if (ss.fail())
            {
                return "Error: invalid datetime format, use ISO 8601 (e.g. 2026-03-05T15:30:00)";
            }

            auto epochTime = std::mktime(&tm);

            if (epochTime == static_cast<std::time_t>(-1))
            {
                return "Error: invalid datetime value";
            }

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
    else if (action == "update")
    {
        if (!params.contains("job_id") || !params["job_id"].is_string())
        {
            return "Error: 'job_id' is required for update action";
        }

        auto jobId = params["job_id"].get<std::string>();

        ionclaw::cron::CronJob patch;

        if (params.contains("name") && params["name"].is_string())
        {
            patch.name = params["name"].get<std::string>();
        }

        if (params.contains("message") && params["message"].is_string())
        {
            patch.payload.message = params["message"].get<std::string>();
        }

        auto tz = params.contains("tz") && params["tz"].is_string()
                      ? params["tz"].get<std::string>()
                      : "";

        // update schedule if a new schedule type is provided
        if (params.contains("every_seconds") && params["every_seconds"].is_number_integer())
        {
            auto seconds = params["every_seconds"].get<int>();

            constexpr int minSeconds = ionclaw::cron::CronService::TICK_INTERVAL_MS / 1000;

            if (seconds < minSeconds)
            {
                return "Error: every_seconds must be at least " + std::to_string(minSeconds);
            }

            patch.schedule.kind = "every";
            patch.schedule.everyMs = static_cast<int64_t>(seconds) * 1000;
        }
        else if (params.contains("cron_expr") && params["cron_expr"].is_string())
        {
            if (!tz.empty() && !ionclaw::cron::CronParser::isValidTimezone(tz))
            {
                return "Error: unknown timezone '" + tz + "'";
            }

            patch.schedule.kind = "cron";
            patch.schedule.expr = params["cron_expr"].get<std::string>();
            patch.schedule.tz = tz;
        }
        else if (params.contains("at") && params["at"].is_string())
        {
            auto atStr = params["at"].get<std::string>();
            std::tm tm{};
            tm.tm_isdst = -1;
            std::istringstream ss(atStr);
            ss.imbue(std::locale::classic());
            ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");

            if (ss.fail())
            {
                return "Error: invalid datetime format, use ISO 8601 (e.g. 2026-03-05T15:30:00)";
            }

            auto epochTime = std::mktime(&tm);

            if (epochTime == static_cast<std::time_t>(-1))
            {
                return "Error: invalid datetime value";
            }

            patch.schedule.kind = "at";
            patch.schedule.atMs = static_cast<int64_t>(epochTime) * 1000;
        }

        if (context.cronService->updateJob(jobId, patch))
        {
            return "Updated job " + jobId;
        }

        return "Job " + jobId + " not found";
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

    return "Error: action must be 'add', 'list', 'update', or 'remove'";
}

ToolSchema CronTool::schema() const
{
    return {
        "cron",
        "Schedule reminders and recurring tasks. Actions: add, list, update, remove.",
        {{"type", "object"},
         {"properties",
          {{"action", {{"type", "string"}, {"enum", nlohmann::json::array({"add", "list", "update", "remove"})}, {"description", "Action to perform"}}},
           {"message", {{"type", "string"}, {"description", "Reminder message (for add/update)"}}},
           {"name", {{"type", "string"}, {"description", "Job display name (for add/update)"}}},
           {"every_seconds", {{"type", "integer"}, {"description", "Interval in seconds (for recurring tasks)"}}},
           {"cron_expr", {{"type", "string"}, {"description", "Cron expression like '0 9 * * *'"}}},
           {"tz", {{"type", "string"}, {"description", "IANA timezone for cron expressions"}}},
           {"at", {{"type", "string"}, {"description", "ISO datetime for one-time execution"}}},
           {"job_id", {{"type", "string"}, {"description", "Job ID (for update/remove)"}}}}},
         {"required", nlohmann::json::array({"action"})}}};
}

} // namespace builtin
} // namespace tool
} // namespace ionclaw
