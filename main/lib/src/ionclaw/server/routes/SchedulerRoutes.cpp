#include "ionclaw/server/Routes.hpp"

#include <iomanip>
#include <sstream>

#include "ionclaw/cron/CronParser.hpp"
#include "ionclaw/cron/CronService.hpp"
#include "ionclaw/cron/CronTypes.hpp"
#include "ionclaw/util/StringHelper.hpp"

namespace ionclaw
{
namespace server
{

void Routes::handleSchedulerList(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp)
{
    auto jobs = cronService->listJobs();
    nlohmann::json result = nlohmann::json::array();

    for (const auto &j : jobs)
    {
        result.push_back({
            {"id", j.id},
            {"name", j.name},
            {"schedule",
             {{"kind", j.schedule.kind},
              {"expr", j.schedule.expr},
              {"every_ms", j.schedule.everyMs},
              {"at_ms", j.schedule.atMs},
              {"tz", j.schedule.tz}}},
            {"payload",
             {{"message", j.payload.message},
              {"channel", j.payload.channel},
              {"to", j.payload.to}}},
            {"state",
             {{"status", j.state.status},
              {"next_run_ms", j.state.nextRunMs},
              {"last_run_ms", j.state.lastRunMs},
              {"run_count", j.state.runCount},
              {"errors", j.state.errors}}},
        });
    }

    sendJson(resp, result);
}

void Routes::handleSchedulerCreate(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp)
{
    try
    {
        auto body = nlohmann::json::parse(readBody(req));

        auto name = body.value("name", "");
        auto message = body.value("message", "");
        auto cronExpr = body.value("cron_expr", "");
        auto everySeconds = body.value("every_seconds", 0);
        auto at = body.value("at", "");
        auto timezone = body.value("timezone", "");
        auto channel = body.value("channel", "");
        auto to = body.value("to", "");

        if (message.empty())
        {
            sendError(resp, "message is required");
            return;
        }

        if (name.empty())
        {
            name = ionclaw::util::StringHelper::utf8SafeTruncate(message, 30);
        }

        if (!timezone.empty() && cronExpr.empty())
        {
            sendError(resp, "timezone can only be used with cron_expr");
            return;
        }

        if (!timezone.empty() && !ionclaw::cron::CronParser::isValidTimezone(timezone))
        {
            sendError(resp, "unknown timezone '" + timezone + "'");
            return;
        }

        // build schedule
        ionclaw::cron::CronSchedule schedule;
        bool deleteAfterRun = false;

        if (!cronExpr.empty())
        {
            if (!ionclaw::cron::CronParser::isValidExpression(cronExpr))
            {
                sendError(resp, "Invalid cron expression: " + cronExpr);
                return;
            }

            schedule.kind = "cron";
            schedule.expr = cronExpr;

            if (!timezone.empty())
            {
                schedule.tz = timezone;
            }
        }
        else if (everySeconds > 0)
        {
            constexpr int minSeconds = ionclaw::cron::CronService::TICK_INTERVAL_MS / 1000;

            if (everySeconds < minSeconds)
            {
                sendError(resp, "every_seconds must be at least " + std::to_string(minSeconds));
                return;
            }

            schedule.kind = "every";
            schedule.everyMs = static_cast<int64_t>(everySeconds) * 1000;
        }
        else if (!at.empty())
        {
            // parse ISO datetime to epoch ms
            std::tm tm{};
            std::istringstream ss(at);
            ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");

            if (ss.fail())
            {
                sendError(resp, "invalid datetime format, use ISO 8601");
                return;
            }

            auto epochTime = std::mktime(&tm);

            if (epochTime <= std::time(nullptr))
            {
                sendError(resp, "scheduled time must be in the future");
                return;
            }

            schedule.kind = "at";
            schedule.atMs = static_cast<int64_t>(epochTime) * 1000;
            deleteAfterRun = true;
        }
        else
        {
            sendError(resp, "one of cron_expr, every_seconds, or at is required");
            return;
        }

        auto job = cronService->addJob(
            name, schedule, message, true, channel, to, deleteAfterRun);

        sendJson(resp, {{"status", "ok"}, {"id", job.id}});
    }
    catch (const std::exception &e)
    {
        sendError(resp, e.what(), 500);
    }
}

void Routes::handleSchedulerUpdate(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &id)
{
    try
    {
        auto body = nlohmann::json::parse(readBody(req));

        auto name = body.value("name", "");
        auto message = body.value("message", "");
        auto cronExpr = body.value("cron_expr", "");
        auto everySeconds = body.value("every_seconds", 0);
        auto at = body.value("at", "");
        auto timezone = body.value("timezone", "");

        if (!timezone.empty() && cronExpr.empty())
        {
            sendError(resp, "timezone can only be used with cron_expr");
            return;
        }

        if (!timezone.empty() && !ionclaw::cron::CronParser::isValidTimezone(timezone))
        {
            sendError(resp, "unknown timezone '" + timezone + "'");
            return;
        }

        ionclaw::cron::CronJob patch;
        patch.name = name;
        patch.payload.message = message;

        // build schedule patch if any schedule field is provided
        if (!cronExpr.empty())
        {
            if (!ionclaw::cron::CronParser::isValidExpression(cronExpr))
            {
                sendError(resp, "Invalid cron expression: " + cronExpr);
                return;
            }

            patch.schedule.kind = "cron";
            patch.schedule.expr = cronExpr;
            patch.schedule.tz = timezone;
        }
        else if (everySeconds > 0)
        {
            constexpr int minSeconds = ionclaw::cron::CronService::TICK_INTERVAL_MS / 1000;

            if (everySeconds < minSeconds)
            {
                sendError(resp, "every_seconds must be at least " + std::to_string(minSeconds));
                return;
            }

            patch.schedule.kind = "every";
            patch.schedule.everyMs = static_cast<int64_t>(everySeconds) * 1000;
        }
        else if (!at.empty())
        {
            std::tm tm{};
            std::istringstream ss(at);
            ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");

            if (ss.fail())
            {
                sendError(resp, "invalid datetime format, use ISO 8601");
                return;
            }

            auto epochTime = std::mktime(&tm);

            if (epochTime <= std::time(nullptr))
            {
                sendError(resp, "scheduled time must be in the future");
                return;
            }

            patch.schedule.kind = "at";
            patch.schedule.atMs = static_cast<int64_t>(epochTime) * 1000;
        }

        if (cronService->updateJob(id, patch))
        {
            sendJson(resp, {{"status", "updated"}});
        }
        else
        {
            sendError(resp, "Job not found", 404);
        }
    }
    catch (const std::exception &e)
    {
        sendError(resp, e.what(), 500);
    }
}

void Routes::handleSchedulerDelete(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp, const std::string &id)
{
    if (cronService->removeJob(id))
    {
        sendJson(resp, {{"status", "deleted"}});
    }
    else
    {
        sendError(resp, "Job not found", 404);
    }
}

} // namespace server
} // namespace ionclaw
