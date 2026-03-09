#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace ionclaw
{
namespace cron
{

class CronParser
{
public:
    // returns next occurrence as epoch ms after now, given a 5-field cron expression
    static int64_t nextRun(const std::string &expr, const std::string &tz = "");

    // validates an IANA timezone string (e.g. "America/New_York")
    static bool isValidTimezone(const std::string &tz);

private:
    static std::vector<int> expandField(const std::string &field, int min, int max);
    static bool matchesField(int value, const std::vector<int> &allowed);

    // protects global TZ environment variable during timezone operations
    static std::mutex tzMutex;
};

} // namespace cron
} // namespace ionclaw
