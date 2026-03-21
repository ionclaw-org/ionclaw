#pragma once

#include <string>

#include "Poco/Net/IPAddress.h"

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
    // check if address is private/reserved (works for both IPv4 and IPv6)
    static bool isPrivateIp(const Poco::Net::IPAddress &addr, bool allowLoopback);

    static void validateUrlImpl(const std::string &url, bool allowLoopback);
};

} // namespace util
} // namespace ionclaw
