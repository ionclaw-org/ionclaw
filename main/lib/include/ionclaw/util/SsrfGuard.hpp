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
    static void validateUrl(const std::string &url);
    static void validateUrlAllowLoopback(const std::string &url);
    static void validatePeerAddress(const Poco::Net::IPAddress &addr, bool allowLoopback = false);

private:
    static bool isBlockedIpv4(const unsigned char *bytes, bool allowLoopback);
    static bool isPrivateIp(const Poco::Net::IPAddress &addr, bool allowLoopback);
    static void validateUrlImpl(const std::string &url, bool allowLoopback);
};

} // namespace util
} // namespace ionclaw
