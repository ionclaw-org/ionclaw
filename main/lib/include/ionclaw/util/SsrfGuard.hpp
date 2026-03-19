#pragma once

#include <string>

namespace ionclaw
{
namespace util
{

class SsrfGuard
{
public:
    // validate url, blocking loopback and private/reserved IPs
    static void validateUrl(const std::string &url);

    // validate url allowing loopback (for local MCP servers)
    static void validateUrlAllowLoopback(const std::string &url);

private:
    static void validateUrlImpl(const std::string &url, bool allowLoopback);
};

} // namespace util
} // namespace ionclaw
