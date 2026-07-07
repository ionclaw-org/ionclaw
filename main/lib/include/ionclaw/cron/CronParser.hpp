#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ionclaw
{
namespace cron
{

class CronParser
{
public:
    static int64_t nextRun(const std::string &expr, const std::string &tz = "");
    static bool isValidTimezone(const std::string &tz);
    static bool isValidExpression(const std::string &expr);

private:
    static int safeStoi(const std::string &s, int fallback);
    static std::vector<int> expandField(const std::string &field, int min, int max);
    static bool matchesField(int value, const std::vector<int> &allowed);
};

} // namespace cron
} // namespace ionclaw
