#include "ionclaw/cron/CronParser.hpp"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <stdexcept>

#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace cron
{

std::mutex CronParser::tzMutex;

namespace
{

// RAII guard for TZ environment variable restoration
class TzGuard
{
public:
    TzGuard(const std::string &tz, std::mutex &mtx)
        : mtx(mtx)
        , overridden(false)
    {
        if (tz.empty())
        {
            return;
        }

#if !defined(_WIN32)
        mtx.lock();

        const char *oldTz = getenv("TZ");
        savedTz = oldTz ? oldTz : "";
        overridden = true;

        setenv("TZ", tz.c_str(), 1);
        tzset();
#endif
    }

    ~TzGuard()
    {
        if (!overridden)
        {
            return;
        }

#if !defined(_WIN32)
        if (savedTz.empty())
        {
            unsetenv("TZ");
        }
        else
        {
            setenv("TZ", savedTz.c_str(), 1);
        }

        tzset();
        mtx.unlock();
#endif
    }

    TzGuard(const TzGuard &) = delete;
    TzGuard &operator=(const TzGuard &) = delete;

    bool isOverridden() const { return overridden; }

private:
    std::mutex &mtx;
    std::string savedTz;
    bool overridden;
};

// safe integer parse with fallback
int safeStoi(const std::string &s, int fallback)
{
    try
    {
        return std::stoi(s);
    }
    catch (const std::exception &)
    {
        return fallback;
    }
}

} // namespace

// parses a single cron field and returns all matching values in [min, max]
std::vector<int> CronParser::expandField(const std::string &field, int min, int max)
{
    std::vector<int> result;

    // split by comma
    std::vector<std::string> parts;
    std::istringstream stream(field);
    std::string part;

    while (std::getline(stream, part, ','))
    {
        parts.push_back(part);
    }

    for (const auto &p : parts)
    {
        // check for step (e.g. */5 or 1-10/2)
        int step = 1;
        std::string base = p;
        auto slashPos = p.find('/');

        if (slashPos != std::string::npos)
        {
            base = p.substr(0, slashPos);
            step = safeStoi(p.substr(slashPos + 1), 1);

            if (step <= 0)
            {
                step = 1;
            }
        }

        // wildcard
        if (base == "*")
        {
            for (int i = min; i <= max; i += step)
            {
                result.push_back(i);
            }

            continue;
        }

        // range (e.g. 1-5)
        auto dashPos = base.find('-');

        if (dashPos != std::string::npos)
        {
            int start = safeStoi(base.substr(0, dashPos), min);
            int end = safeStoi(base.substr(dashPos + 1), max);

            for (int i = start; i <= end; i += step)
            {
                if (i >= min && i <= max)
                {
                    result.push_back(i);
                }
            }

            continue;
        }

        // single value
        int val = safeStoi(base, -1);

        if (val >= min && val <= max)
        {
            result.push_back(val);
        }
    }

    return result;
}

bool CronParser::matchesField(int value, const std::vector<int> &allowed)
{
    for (int v : allowed)
    {
        if (v == value)
        {
            return true;
        }
    }

    return false;
}

bool CronParser::isValidTimezone(const std::string &tz)
{
    if (tz.empty())
    {
        return false;
    }

    if (tz == "UTC" || tz == "GMT")
    {
        return true;
    }

#if !defined(_WIN32)
    TzGuard guard(tz, tzMutex);

    auto now = std::time(nullptr);
    std::tm tm1{};
    localtime_r(&now, &tm1);

    // valid IANA timezones produce short abbreviations (e.g. "EST", "PST")
    // invalid ones echo the input string as abbreviation
    std::string tzAbbrev = tm1.tm_zone ? tm1.tm_zone : "";

    if (tz.find('/') != std::string::npos && tzAbbrev == tz)
    {
        return false;
    }

    return tz.find('/') != std::string::npos;
#else
    // Windows: accept common formats
    return tz.find('/') != std::string::npos;
#endif
}

int64_t CronParser::nextRun(const std::string &expr, const std::string &tz)
{
    // parse 5-field cron: minute hour day-of-month month day-of-week
    std::istringstream stream(expr);
    std::string fields[5];
    int fieldCount = 0;

    while (fieldCount < 5 && stream >> fields[fieldCount])
    {
        fieldCount++;
    }

    if (fieldCount != 5)
    {
        spdlog::warn("[CronParser] invalid cron expression (need 5 fields): {}", expr);
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   (now + std::chrono::minutes(1)).time_since_epoch())
            .count();
    }

    auto minutes = expandField(fields[0], 0, 59);
    auto hours = expandField(fields[1], 0, 23);
    auto daysOfMonth = expandField(fields[2], 1, 31);
    auto months = expandField(fields[3], 1, 12);
    auto daysOfWeek = expandField(fields[4], 0, 6);

    // RAII guard handles TZ set/restore and mutex lock/unlock
    TzGuard guard(tz, tzMutex);

    auto now = std::chrono::system_clock::now();
    auto nowTime = std::chrono::system_clock::to_time_t(now);

    std::tm tm{};

#if defined(_WIN32)
    localtime_s(&tm, &nowTime);
#else
    localtime_r(&nowTime, &tm);
#endif

    // advance one minute from now to find the next match
    tm.tm_sec = 0;
    tm.tm_min++;

    if (tm.tm_min >= 60)
    {
        tm.tm_min = 0;
        tm.tm_hour++;
    }

    // search up to 366 days ahead
    static constexpr int MAX_ITERATIONS = 366 * 24 * 60;

    for (int i = 0; i < MAX_ITERATIONS; i++)
    {
        // normalize the time struct
        std::mktime(&tm);

        int dow = tm.tm_wday; // 0=Sunday

        if (matchesField(tm.tm_mon + 1, months) &&
            matchesField(tm.tm_mday, daysOfMonth) &&
            matchesField(dow, daysOfWeek) &&
            matchesField(tm.tm_hour, hours) &&
            matchesField(tm.tm_min, minutes))
        {
            tm.tm_sec = 0;
            std::time_t result = std::mktime(&tm);
            return static_cast<int64_t>(result) * 1000;
        }

        // advance one minute
        tm.tm_min++;

        if (tm.tm_min >= 60)
        {
            tm.tm_min = 0;
            tm.tm_hour++;

            if (tm.tm_hour >= 24)
            {
                tm.tm_hour = 0;
                tm.tm_mday++;
            }
        }
    }

    // no match found within a year, fallback to 1 hour from now
    spdlog::warn("[CronParser] no match found for cron expression: {}", expr);
    auto fallback = std::chrono::system_clock::now() + std::chrono::hours(1);
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               fallback.time_since_epoch())
        .count();
}

} // namespace cron
} // namespace ionclaw
