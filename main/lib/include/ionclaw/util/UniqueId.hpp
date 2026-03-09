#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace ionclaw
{
namespace util
{

class UniqueId
{
public:
    static std::string uuid();
    static std::string shortId();
    static std::string sequential(const std::string &prefix);

private:
    static std::atomic<uint64_t> counter;
};

} // namespace util
} // namespace ionclaw
