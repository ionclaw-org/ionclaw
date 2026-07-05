#pragma once

#include <ctime>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace ionclaw
{
namespace cron
{

class WindowsTimeZone
{
public:
    static bool isSupported(const std::string &ianaName);
    static bool utcToLocal(const std::string &ianaName, std::time_t utcEpoch, std::tm &localTm);
    static bool localToUtc(const std::string &ianaName, const std::tm &localTm, std::time_t &utcEpoch);

private:
    static std::string ianaToWindowsKey(const std::string &ianaName);

#ifdef _WIN32
    static bool loadTimeZoneInfo(const std::string &windowsKey, TIME_ZONE_INFORMATION &tzi);
    static std::time_t systemTimeToEpoch(const SYSTEMTIME &st);
    static SYSTEMTIME epochToSystemTime(std::time_t epoch);
#endif
};

} // namespace cron
} // namespace ionclaw
