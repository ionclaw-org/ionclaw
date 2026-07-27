#include "ionclaw/util/UniqueId.hpp"

#include <iomanip>
#include <sstream>

#include "ionclaw/util/RandomHelper.hpp"

namespace ionclaw
{
namespace util
{

std::atomic<uint64_t> UniqueId::counter{0};

std::string UniqueId::uuid()
{
    auto hi = RandomHelper::secureUint64();
    auto lo = RandomHelper::secureUint64();

    // set version 4: bits 12-15 of time_hi_and_version
    hi = (hi & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;

    // set variant 1: bits 6-7 of clock_seq_hi_and_reserved
    lo = (lo & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    oss << std::setw(8) << ((hi >> 32) & 0xFFFFFFFF);
    oss << "-";
    oss << std::setw(4) << ((hi >> 16) & 0xFFFF);
    oss << "-";
    oss << std::setw(4) << (hi & 0xFFFF);
    oss << "-";
    oss << std::setw(4) << ((lo >> 48) & 0xFFFF);
    oss << "-";
    oss << std::setw(12) << (lo & 0xFFFFFFFFFFFFULL);

    return oss.str();
}

std::string UniqueId::shortId()
{
    auto value = RandomHelper::secureUint64();

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    oss << std::setw(12) << (value & 0xFFFFFFFFFFFFULL);

    return oss.str();
}

} // namespace util
} // namespace ionclaw
