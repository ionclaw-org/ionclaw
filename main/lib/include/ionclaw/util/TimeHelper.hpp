#pragma once

#include <cstdint>
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
    static std::string formatDuration(int64_t seconds);
    static int64_t epochMs();
    static int64_t diffSeconds(const std::string &from, const std::string &to);
};

} // namespace util
} // namespace ionclaw
