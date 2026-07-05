#include "ionclaw/cron/WindowsTimeZone.hpp"

#include <unordered_map>

namespace ionclaw
{
namespace cron
{

std::string WindowsTimeZone::ianaToWindowsKey(const std::string &ianaName)
{
    // iana to windows time zone key, derived from the unicode cldr windowsZones mapping
    static const std::unordered_map<std::string, std::string> map = {
        {"Etc/UTC", "UTC"},
        {"Etc/GMT", "UTC"},
        {"UTC", "UTC"},
        {"America/New_York", "Eastern Standard Time"},
        {"America/Detroit", "Eastern Standard Time"},
        {"America/Toronto", "Eastern Standard Time"},
        {"America/Nassau", "Eastern Standard Time"},
        {"America/Indiana/Indianapolis", "US Eastern Standard Time"},
        {"America/Chicago", "Central Standard Time"},
        {"America/Winnipeg", "Central Standard Time"},
        {"America/Mexico_City", "Central Standard Time (Mexico)"},
        {"America/Denver", "Mountain Standard Time"},
        {"America/Edmonton", "Mountain Standard Time"},
        {"America/Phoenix", "US Mountain Standard Time"},
        {"America/Los_Angeles", "Pacific Standard Time"},
        {"America/Vancouver", "Pacific Standard Time"},
        {"America/Tijuana", "Pacific Standard Time (Mexico)"},
        {"America/Anchorage", "Alaskan Standard Time"},
        {"America/Adak", "Aleutian Standard Time"},
        {"Pacific/Honolulu", "Hawaiian Standard Time"},
        {"America/Halifax", "Atlantic Standard Time"},
        {"America/St_Johns", "Newfoundland Standard Time"},
        {"America/Sao_Paulo", "E. South America Standard Time"},
        {"America/Argentina/Buenos_Aires", "Argentina Standard Time"},
        {"America/Santiago", "Pacific SA Standard Time"},
        {"America/Bogota", "SA Pacific Standard Time"},
        {"America/Lima", "SA Pacific Standard Time"},
        {"America/Caracas", "Venezuela Standard Time"},
        {"America/La_Paz", "SA Western Standard Time"},
        {"America/Guatemala", "Central America Standard Time"},
        {"America/Costa_Rica", "Central America Standard Time"},
        {"America/Regina", "Canada Central Standard Time"},
        {"America/Cuiaba", "Central Brazilian Standard Time"},
        {"America/Godthab", "Greenland Standard Time"},
        {"America/Montevideo", "Montevideo Standard Time"},
        {"Atlantic/Cape_Verde", "Cape Verde Standard Time"},
        {"Atlantic/Azores", "Azores Standard Time"},
        {"Atlantic/Reykjavik", "Greenwich Standard Time"},
        {"Europe/London", "GMT Standard Time"},
        {"Europe/Dublin", "GMT Standard Time"},
        {"Europe/Lisbon", "GMT Standard Time"},
        {"Africa/Casablanca", "Morocco Standard Time"},
        {"Europe/Berlin", "W. Europe Standard Time"},
        {"Europe/Rome", "W. Europe Standard Time"},
        {"Europe/Madrid", "Romance Standard Time"},
        {"Europe/Paris", "Romance Standard Time"},
        {"Europe/Brussels", "Romance Standard Time"},
        {"Europe/Amsterdam", "W. Europe Standard Time"},
        {"Europe/Vienna", "W. Europe Standard Time"},
        {"Europe/Zurich", "W. Europe Standard Time"},
        {"Europe/Stockholm", "W. Europe Standard Time"},
        {"Europe/Oslo", "W. Europe Standard Time"},
        {"Europe/Copenhagen", "Romance Standard Time"},
        {"Europe/Warsaw", "Central European Standard Time"},
        {"Europe/Prague", "Central Europe Standard Time"},
        {"Europe/Budapest", "Central Europe Standard Time"},
        {"Europe/Belgrade", "Central Europe Standard Time"},
        {"Europe/Athens", "GTB Standard Time"},
        {"Europe/Bucharest", "GTB Standard Time"},
        {"Europe/Helsinki", "FLE Standard Time"},
        {"Europe/Kiev", "FLE Standard Time"},
        {"Europe/Kyiv", "FLE Standard Time"},
        {"Europe/Riga", "FLE Standard Time"},
        {"Europe/Sofia", "FLE Standard Time"},
        {"Europe/Istanbul", "Turkey Standard Time"},
        {"Europe/Moscow", "Russian Standard Time"},
        {"Europe/Minsk", "Belarus Standard Time"},
        {"Africa/Lagos", "W. Central Africa Standard Time"},
        {"Africa/Johannesburg", "South Africa Standard Time"},
        {"Africa/Cairo", "Egypt Standard Time"},
        {"Africa/Nairobi", "E. Africa Standard Time"},
        {"Asia/Jerusalem", "Israel Standard Time"},
        {"Asia/Beirut", "Middle East Standard Time"},
        {"Asia/Amman", "Jordan Standard Time"},
        {"Asia/Baghdad", "Arabic Standard Time"},
        {"Asia/Riyadh", "Arab Standard Time"},
        {"Asia/Kuwait", "Arab Standard Time"},
        {"Asia/Dubai", "Arabian Standard Time"},
        {"Asia/Tehran", "Iran Standard Time"},
        {"Asia/Baku", "Azerbaijan Standard Time"},
        {"Asia/Yerevan", "Caucasus Standard Time"},
        {"Asia/Tbilisi", "Georgian Standard Time"},
        {"Asia/Kabul", "Afghanistan Standard Time"},
        {"Asia/Karachi", "Pakistan Standard Time"},
        {"Asia/Tashkent", "West Asia Standard Time"},
        {"Asia/Yekaterinburg", "Ekaterinburg Standard Time"},
        {"Asia/Kolkata", "India Standard Time"},
        {"Asia/Calcutta", "India Standard Time"},
        {"Asia/Colombo", "Sri Lanka Standard Time"},
        {"Asia/Kathmandu", "Nepal Standard Time"},
        {"Asia/Dhaka", "Bangladesh Standard Time"},
        {"Asia/Almaty", "Central Asia Standard Time"},
        {"Asia/Yangon", "Myanmar Standard Time"},
        {"Asia/Bangkok", "SE Asia Standard Time"},
        {"Asia/Jakarta", "SE Asia Standard Time"},
        {"Asia/Ho_Chi_Minh", "SE Asia Standard Time"},
        {"Asia/Novosibirsk", "N. Central Asia Standard Time"},
        {"Asia/Shanghai", "China Standard Time"},
        {"Asia/Hong_Kong", "China Standard Time"},
        {"Asia/Singapore", "Singapore Standard Time"},
        {"Asia/Kuala_Lumpur", "Singapore Standard Time"},
        {"Asia/Taipei", "Taipei Standard Time"},
        {"Asia/Manila", "Singapore Standard Time"},
        {"Australia/Perth", "W. Australia Standard Time"},
        {"Asia/Irkutsk", "North Asia East Standard Time"},
        {"Asia/Ulaanbaatar", "Ulaanbaatar Standard Time"},
        {"Asia/Tokyo", "Tokyo Standard Time"},
        {"Asia/Seoul", "Korea Standard Time"},
        {"Asia/Yakutsk", "Yakutsk Standard Time"},
        {"Australia/Adelaide", "Cen. Australia Standard Time"},
        {"Australia/Darwin", "AUS Central Standard Time"},
        {"Australia/Brisbane", "E. Australia Standard Time"},
        {"Australia/Sydney", "AUS Eastern Standard Time"},
        {"Australia/Melbourne", "AUS Eastern Standard Time"},
        {"Australia/Hobart", "Tasmania Standard Time"},
        {"Asia/Vladivostok", "Vladivostok Standard Time"},
        {"Pacific/Guadalcanal", "Central Pacific Standard Time"},
        {"Pacific/Noumea", "Central Pacific Standard Time"},
        {"Pacific/Auckland", "New Zealand Standard Time"},
        {"Pacific/Fiji", "Fiji Standard Time"},
        {"Asia/Kamchatka", "Kamchatka Standard Time"},
        {"Pacific/Tongatapu", "Tonga Standard Time"},
        {"Pacific/Apia", "Samoa Standard Time"},
    };

    auto it = map.find(ianaName);
    return it != map.end() ? it->second : std::string();
}

#ifdef _WIN32

std::time_t WindowsTimeZone::systemTimeToEpoch(const SYSTEMTIME &st)
{
    FILETIME ft;

    if (!SystemTimeToFileTime(&st, &ft))
    {
        return 0;
    }

    ULONGLONG ticks = (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return static_cast<std::time_t>(ticks / 10000000ULL - 11644473600ULL);
}

SYSTEMTIME WindowsTimeZone::epochToSystemTime(std::time_t epoch)
{
    ULONGLONG ticks = (static_cast<ULONGLONG>(epoch) + 11644473600ULL) * 10000000ULL;
    FILETIME ft;
    ft.dwLowDateTime = static_cast<DWORD>(ticks & 0xFFFFFFFFULL);
    ft.dwHighDateTime = static_cast<DWORD>(ticks >> 32);

    SYSTEMTIME st = {};
    FileTimeToSystemTime(&ft, &st);
    return st;
}

bool WindowsTimeZone::loadTimeZoneInfo(const std::string &windowsKey, TIME_ZONE_INFORMATION &tzi)
{
    struct RegTziFormat
    {
        LONG bias;
        LONG standardBias;
        LONG daylightBias;
        SYSTEMTIME standardDate;
        SYSTEMTIME daylightDate;
    };

    std::string path = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones\\" + windowsKey;

    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
    {
        return false;
    }

    RegTziFormat reg = {};
    DWORD size = sizeof(reg);
    auto status = RegQueryValueExA(key, "TZI", nullptr, nullptr, reinterpret_cast<LPBYTE>(&reg), &size);
    RegCloseKey(key);

    if (status != ERROR_SUCCESS || size != sizeof(reg))
    {
        return false;
    }

    tzi = {};
    tzi.Bias = reg.bias;
    tzi.StandardBias = reg.standardBias;
    tzi.DaylightBias = reg.daylightBias;
    tzi.StandardDate = reg.standardDate;
    tzi.DaylightDate = reg.daylightDate;
    return true;
}

bool WindowsTimeZone::isSupported(const std::string &ianaName)
{
    if (ianaName.empty())
    {
        return false;
    }

    auto key = ianaToWindowsKey(ianaName);
    if (key.empty())
    {
        return false;
    }

    TIME_ZONE_INFORMATION tzi;
    return loadTimeZoneInfo(key, tzi);
}

bool WindowsTimeZone::utcToLocal(const std::string &ianaName, std::time_t utcEpoch, std::tm &localTm)
{
    auto key = ianaToWindowsKey(ianaName);
    TIME_ZONE_INFORMATION tzi;

    if (key.empty() || !loadTimeZoneInfo(key, tzi))
    {
        return false;
    }

    SYSTEMTIME utcSt = epochToSystemTime(utcEpoch);
    SYSTEMTIME localSt = {};

    if (!SystemTimeToTzSpecificLocalTime(&tzi, &utcSt, &localSt))
    {
        return false;
    }

    localTm = {};
    localTm.tm_year = localSt.wYear - 1900;
    localTm.tm_mon = localSt.wMonth - 1;
    localTm.tm_mday = localSt.wDay;
    localTm.tm_hour = localSt.wHour;
    localTm.tm_min = localSt.wMinute;
    localTm.tm_sec = localSt.wSecond;
    localTm.tm_wday = localSt.wDayOfWeek;
    localTm.tm_isdst = -1;
    return true;
}

bool WindowsTimeZone::localToUtc(const std::string &ianaName, const std::tm &localTm, std::time_t &utcEpoch)
{
    auto key = ianaToWindowsKey(ianaName);
    TIME_ZONE_INFORMATION tzi;

    if (key.empty() || !loadTimeZoneInfo(key, tzi))
    {
        return false;
    }

    SYSTEMTIME localSt = {};
    localSt.wYear = static_cast<WORD>(localTm.tm_year + 1900);
    localSt.wMonth = static_cast<WORD>(localTm.tm_mon + 1);
    localSt.wDay = static_cast<WORD>(localTm.tm_mday);
    localSt.wHour = static_cast<WORD>(localTm.tm_hour);
    localSt.wMinute = static_cast<WORD>(localTm.tm_min);
    localSt.wSecond = static_cast<WORD>(localTm.tm_sec);

    SYSTEMTIME utcSt = {};

    if (!TzSpecificLocalTimeToSystemTime(&tzi, &localSt, &utcSt))
    {
        return false;
    }

    utcEpoch = systemTimeToEpoch(utcSt);
    return true;
}

#else

bool WindowsTimeZone::isSupported(const std::string &)
{
    return false;
}

bool WindowsTimeZone::utcToLocal(const std::string &, std::time_t, std::tm &)
{
    return false;
}

bool WindowsTimeZone::localToUtc(const std::string &, const std::tm &, std::time_t &)
{
    return false;
}

#endif

} // namespace cron
} // namespace ionclaw
