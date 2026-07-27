#pragma once

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
};

} // namespace util
} // namespace ionclaw
