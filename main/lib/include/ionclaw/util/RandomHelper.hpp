#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ionclaw
{
namespace util
{

class RandomHelper
{
public:
    static std::string secureHex(std::size_t byteCount);
    static uint64_t secureUint64();
    static bool constantTimeEquals(const std::string &a, const std::string &b);
};

} // namespace util
} // namespace ionclaw
