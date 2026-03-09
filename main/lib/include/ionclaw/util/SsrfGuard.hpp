#pragma once

#include <string>

namespace ionclaw
{
namespace util
{

class SsrfGuard
{
public:
    static void validateUrl(const std::string &url);
};

} // namespace util
} // namespace ionclaw
