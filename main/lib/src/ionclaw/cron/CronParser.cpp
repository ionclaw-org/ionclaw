#include "ionclaw/cron/CronParser.hpp"

#include <chrono>
#include <ctime>
#include <sstream>
#include <stdexcept>

#include "spdlog/spdlog.h"

#include "ionclaw/cron/WindowsTimeZone.hpp"

#if defined(IONCLAW_HAS_TZ_LIB)
#include "date/tz.h"
#endif

namespace ionclaw
{
namespace cron
{

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

#if defined(IONCLAW_HAS_TZ_LIB)
    // the iana database is authoritative, so a zone locate_zone accepts is valid
    try
    {
        date::locate_zone(tz);
        return true;
    }
    catch (const std::exception &)
    {
        return false;
    }
#else
    // windows has no zoneinfo database, so accept the region/city shape and let WindowsTimeZone map it
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

    // verify each field produces at least one valid value, day-of-week accepts 7 as sunday
    static const int mins[] = {0, 0, 1, 1, 0};
    static const int maxs[] = {59, 23, 31, 12, 7};

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
        spdlog::warn("[CronParser] Invalid cron expression (need 5 fields): {}", expr);
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>((now + std::chrono::minutes(1)).time_since_epoch()).count();
    }

    auto minutes = expandField(fields[0], 0, 59);
    auto hours = expandField(fields[1], 0, 23);
    auto daysOfMonth = expandField(fields[2], 1, 31);
    auto months = expandField(fields[3], 1, 12);

    // day-of-week accepts 7 as sunday, normalized to 0 to match tm_wday
    auto daysOfWeek = expandField(fields[4], 0, 7);

    for (auto &dow : daysOfWeek)
    {
        if (dow == 7)
        {
            dow = 0;
        }
    }

    // vixie cron matches on either day field when both are restricted, otherwise on both
    bool domRestricted = fields[2] != "*" && !fields[2].starts_with("*/");
    bool dowRestricted = fields[4] != "*" && !fields[4].starts_with("*/");
    bool dayUnion = domRestricted && dowRestricted;

    // search up to 366 days ahead
    static constexpr int MAX_ITERATIONS = 366 * 24 * 60;

#if defined(_WIN32)
    // windows has no iana tz database, so resolve the zone to native rules for dst-correct scheduling
    if (!tz.empty() && WindowsTimeZone::isSupported(tz))
    {
        auto nowEpoch = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm ltm{};

        if (WindowsTimeZone::utcToLocal(tz, nowEpoch, ltm))
        {
            ltm.tm_sec = 0;
            ltm.tm_min++;

            for (int i = 0; i < MAX_ITERATIONS; i++)
            {
                // _mkgmtime normalizes the calendar fields and weekday without applying any timezone
                std::time_t normalized = _mkgmtime(&ltm);
                gmtime_s(&ltm, &normalized);

                bool dayMatch = dayUnion ? (matchesField(ltm.tm_mday, daysOfMonth) || matchesField(ltm.tm_wday, daysOfWeek)) : (matchesField(ltm.tm_mday, daysOfMonth) && matchesField(ltm.tm_wday, daysOfWeek));

                if (matchesField(ltm.tm_mon + 1, months) && dayMatch && matchesField(ltm.tm_hour, hours) && matchesField(ltm.tm_min, minutes))
                {
                    std::time_t utcResult = 0;

                    if (WindowsTimeZone::localToUtc(tz, ltm, utcResult))
                    {
                        return static_cast<int64_t>(utcResult) * 1000;
                    }

                    break;
                }

                ltm.tm_min++;
            }
        }
    }
#endif

#if defined(IONCLAW_HAS_TZ_LIB)
    // resolve the zone from the system iana database, which is thread-safe and needs no global TZ mutation
    const date::time_zone *zone = nullptr;

    try
    {
        zone = tz.empty() ? date::current_zone() : date::locate_zone(tz);
    }
    catch (const std::exception &e)
    {
        spdlog::warn("[CronParser] Unknown timezone '{}', using system local: {}", tz, e.what());
        zone = date::current_zone();
    }

    // the cron field vectors are named minutes/hours, so the chrono types are fully qualified to avoid shadowing
    auto nowSys = std::chrono::floor<std::chrono::minutes>(std::chrono::system_clock::now());
    auto candidate = date::make_zoned(zone, nowSys).get_local_time() + std::chrono::minutes(1);

    for (int i = 0; i < MAX_ITERATIONS; i++)
    {
        auto localDay = std::chrono::floor<date::days>(candidate);
        date::year_month_day ymd{localDay};
        date::hh_mm_ss tod{candidate - localDay};
        date::weekday wd{localDay};

        int mon = static_cast<int>(unsigned(ymd.month()));
        int mday = static_cast<int>(unsigned(ymd.day()));
        int hour = static_cast<int>(tod.hours().count());
        int minute = static_cast<int>(tod.minutes().count());
        int dow = static_cast<int>(wd.c_encoding()); // 0=Sunday

        bool dayMatch = dayUnion ? (matchesField(mday, daysOfMonth) || matchesField(dow, daysOfWeek)) : (matchesField(mday, daysOfMonth) && matchesField(dow, daysOfWeek));

        if (matchesField(mon, months) && dayMatch && matchesField(hour, hours) && matchesField(minute, minutes))
        {
            // interpret the matched wall-clock in the zone, choosing the earliest instant across a dst fold
            auto utc = date::make_zoned(zone, candidate, date::choose::earliest).get_sys_time();
            return std::chrono::duration_cast<std::chrono::milliseconds>(utc.time_since_epoch()).count();
        }

        candidate += std::chrono::minutes(1);
    }
#else
    // fallback in device-local time where the tz library is absent: windows zones its native resolver could not map, and apple mobile without a bundled zone database
    auto now = std::chrono::system_clock::now();
    auto nowTime = std::chrono::system_clock::to_time_t(now);

    std::tm tm{};

#if defined(_WIN32)
    localtime_s(&tm, &nowTime);
#else
    localtime_r(&nowTime, &tm);
#endif

    tm.tm_sec = 0;
    tm.tm_min++;

    for (int i = 0; i < MAX_ITERATIONS; i++)
    {
        tm.tm_isdst = -1;
        std::mktime(&tm);

        int dow = tm.tm_wday; // 0=Sunday
        bool dayMatch = dayUnion ? (matchesField(tm.tm_mday, daysOfMonth) || matchesField(dow, daysOfWeek)) : (matchesField(tm.tm_mday, daysOfMonth) && matchesField(dow, daysOfWeek));

        if (matchesField(tm.tm_mon + 1, months) && dayMatch && matchesField(tm.tm_hour, hours) && matchesField(tm.tm_min, minutes))
        {
            tm.tm_sec = 0;
            tm.tm_isdst = -1;
            return static_cast<int64_t>(std::mktime(&tm)) * 1000;
        }

        tm.tm_min++;
    }
#endif

    // no match found within a year, fall back to one hour from now
    spdlog::warn("[CronParser] No match found for cron expression: {}", expr);
    auto fallback = std::chrono::system_clock::now() + std::chrono::hours(1);
    return std::chrono::duration_cast<std::chrono::milliseconds>(fallback.time_since_epoch()).count();
}

} // namespace cron
} // namespace ionclaw
