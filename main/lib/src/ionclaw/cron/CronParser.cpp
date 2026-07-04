#include "ionclaw/cron/CronParser.hpp"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <sstream>
#include <stdexcept>

#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace cron
{

std::mutex CronParser::tzMutex;

CronParser::TzGuard::TzGuard(const std::string &tz, std::mutex &mtx)
    : lock(mtx)
    , overridden(false)
{
    // always lock to prevent concurrent TZ mutation, since even an empty tz still reads the global TZ via mktime/localtime_r
    if (tz.empty())
    {
        return;
    }

#if !defined(_WIN32)
    const char *oldTz = getenv("TZ");
    savedTz = oldTz ? oldTz : "";
    overridden = true;

    setenv("TZ", tz.c_str(), 1);
    tzset();
#endif
}

CronParser::TzGuard::~TzGuard()
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
    // lock is released automatically by unique_lock destructor
#endif
}

int CronParser::safeStoi(const std::string &s, int fallback)
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

    // reject path traversal before touching the filesystem or the TZ environment
    if (tz.find("..") != std::string::npos || tz.front() == '/')
    {
        return false;
    }

#if !defined(_WIN32)
    // a valid IANA zone has a matching entry in the system zoneinfo database
    bool haveDatabase = false;

    for (const auto *dir : {"/usr/share/zoneinfo", "/etc/zoneinfo"})
    {
        std::error_code ec;

        if (!std::filesystem::is_directory(dir, ec))
        {
            continue;
        }

        haveDatabase = true;

        if (std::filesystem::is_regular_file(std::filesystem::path(dir) / tz, ec))
        {
            return true;
        }
    }

    // when the database is present a missing entry means the zone is invalid
    if (haveDatabase)
    {
        return false;
    }

    // slim environments ship no zoneinfo database, so fall back to the region/city shape
    return tz.find('/') != std::string::npos;
#else
    // windows lacks the zoneinfo database, so accept the region/city shape
    return tz.find('/') != std::string::npos;
#endif
}

bool CronParser::isValidExpression(const std::string &expr)
{
    std::istringstream stream(expr);
    std::string fields[5];
    int fieldCount = 0;

    while (fieldCount < 5 && stream >> fields[fieldCount])
    {
        fieldCount++;
    }

    // reject anything that is not exactly five fields, including trailing extra tokens
    std::string extra;

    if (fieldCount != 5 || stream >> extra)
    {
        return false;
    }

    // verify each field produces at least one valid value
    static const int mins[] = {0, 0, 1, 1, 0};
    static const int maxs[] = {59, 23, 31, 12, 6};

    for (int i = 0; i < 5; ++i)
    {
        auto values = expandField(fields[i], mins[i], maxs[i]);

        if (values.empty())
        {
            return false;
        }
    }

    return true;
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
        return std::chrono::duration_cast<std::chrono::milliseconds>((now + std::chrono::minutes(1)).time_since_epoch()).count();
    }

    auto minutes = expandField(fields[0], 0, 59);
    auto hours = expandField(fields[1], 0, 23);
    auto daysOfMonth = expandField(fields[2], 1, 31);
    auto months = expandField(fields[3], 1, 12);
    auto daysOfWeek = expandField(fields[4], 0, 6);

    // raii guard handles TZ set/restore and mutex lock/unlock
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

        if (matchesField(tm.tm_mon + 1, months) && matchesField(tm.tm_mday, daysOfMonth) && matchesField(dow, daysOfWeek) && matchesField(tm.tm_hour, hours) && matchesField(tm.tm_min, minutes))
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
    return std::chrono::duration_cast<std::chrono::milliseconds>(fallback.time_since_epoch()).count();
}

} // namespace cron
} // namespace ionclaw
