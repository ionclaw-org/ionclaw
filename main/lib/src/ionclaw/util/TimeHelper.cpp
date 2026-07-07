#include "ionclaw/util/TimeHelper.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#if defined(IONCLAW_HAS_TZ_LIB)
#include "date/tz.h"
#elif defined(_WIN32)
#include "ionclaw/cron/WindowsTimeZone.hpp"
#endif

namespace ionclaw
{
namespace util
{

std::string TimeHelper::now()
{
    auto tp = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(tp);

    std::tm gmt{};

#if defined(_WIN32)
    gmtime_s(&gmt, &time);
#else
    gmtime_r(&time, &gmt);
#endif

    std::ostringstream oss;
    oss << std::put_time(&gmt, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::string TimeHelper::nowLocal()
{
    auto tp = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(tp);

    std::tm local{};

#if defined(_WIN32)
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif

    std::ostringstream oss;
    oss << std::put_time(&local, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

std::string TimeHelper::nowInZone(const std::string &timezone)
{
    // an empty or unresolvable zone falls back to the server's local clock
    if (timezone.empty())
    {
        return nowLocal();
    }

#if defined(IONCLAW_HAS_TZ_LIB)
    try
    {
        auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
        return date::format("%Y-%m-%dT%H:%M:%S", date::make_zoned(date::locate_zone(timezone), now));
    }
    catch (const std::exception &)
    {
        return nowLocal();
    }
#elif defined(_WIN32)
    auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};

    if (cron::WindowsTimeZone::isSupported(timezone) && cron::WindowsTimeZone::utcToLocal(timezone, time, local))
    {
        std::ostringstream oss;
        oss << std::put_time(&local, "%Y-%m-%dT%H:%M:%S");
        return oss.str();
    }

    return nowLocal();
#else
    return nowLocal();
#endif
}

int64_t TimeHelper::epochMs()
{
    auto tp = std::chrono::system_clock::now();
    auto duration = tp.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

int64_t TimeHelper::diffSeconds(const std::string &from, const std::string &to)
{
    // parse ISO 8601 timestamps: YYYY-MM-DDTHH:MM:SSZ
    auto parse = [](const std::string &ts) -> std::time_t
    {
        std::tm tm{};
        std::istringstream iss(ts);
        iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");

        if (iss.fail())
        {
            return static_cast<std::time_t>(-1);
        }

#if defined(_WIN32)
        return _mkgmtime(&tm);
#else
        return timegm(&tm);
#endif
    };

    auto fromTime = parse(from);
    auto toTime = parse(to);

    // a malformed timestamp would otherwise diff against 1970 and return a decades-off value
    if (fromTime == static_cast<std::time_t>(-1) || toTime == static_cast<std::time_t>(-1))
    {
        return 0;
    }

    return static_cast<int64_t>(std::difftime(toTime, fromTime));
}

} // namespace util
} // namespace ionclaw
