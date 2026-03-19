#include "ionclaw/util/TimeHelper.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

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
            return 0;
        }

#if defined(_WIN32)
        return _mkgmtime(&tm);
#else
        return timegm(&tm);
#endif
    };

    auto fromTime = parse(from);
    auto toTime = parse(to);
    return static_cast<int64_t>(std::difftime(toTime, fromTime));
}

} // namespace util
} // namespace ionclaw
