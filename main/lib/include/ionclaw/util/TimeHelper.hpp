#pragma once

#include <cstdint>
#include <ctime>
#include <string>

namespace ionclaw
{
namespace util
{

class TimeHelper
{
public:
    static std::string now();
    static std::string nowLocal();
    static std::string nowInZone(const std::string &timezone);
    static int64_t epochMs();
    static int64_t diffSeconds(const std::string &from, const std::string &to);

    // interpret a broken-down time as utc and return the epoch seconds, or -1 on failure
    static std::time_t timegmUtc(std::tm &tm);
};

} // namespace util
} // namespace ionclaw
