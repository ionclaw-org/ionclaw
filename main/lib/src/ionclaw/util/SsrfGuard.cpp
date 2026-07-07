#include "ionclaw/util/SsrfGuard.hpp"

#include <stdexcept>

#include "Poco/Net/DNS.h"
#include "Poco/Net/IPAddress.h"
#include "Poco/Net/NetException.h"
#include "Poco/URI.h"

namespace ionclaw
{
namespace util
{

// covers loopback, private, cgnat, cloud-metadata, and reserved ipv4 ranges that Poco does not classify
bool SsrfGuard::isBlockedIpv4(const unsigned char *b, bool allowLoopback)
{
    // 127.0.0.0/8 loopback
    if (b[0] == 127)
    {
        return !allowLoopback;
    }

    // 0.0.0.0/8, 10.0.0.0/8
    if (b[0] == 0 || b[0] == 10)
    {
        return true;
    }

    // 100.64.0.0/10 carrier-grade nat (hosts alibaba metadata 100.100.100.200)
    if (b[0] == 100 && b[1] >= 64 && b[1] <= 127)
    {
        return true;
    }

    // 169.254.0.0/16 link-local (aws/azure/gcp metadata)
    if (b[0] == 169 && b[1] == 254)
    {
        return true;
    }

    // 172.16.0.0/12
    if (b[0] == 172 && b[1] >= 16 && b[1] <= 31)
    {
        return true;
    }

    // 192.0.0.0/24 ietf protocol assignments (hosts oracle metadata 192.0.0.192)
    if (b[0] == 192 && b[1] == 0 && b[2] == 0)
    {
        return true;
    }

    // 192.88.99.0/24 6to4 relay anycast
    if (b[0] == 192 && b[1] == 88 && b[2] == 99)
    {
        return true;
    }

    // 192.168.0.0/16
    if (b[0] == 192 && b[1] == 168)
    {
        return true;
    }

    // 224.0.0.0/4 multicast and 240.0.0.0/4 reserved (includes 255.255.255.255)
    if (b[0] >= 224)
    {
        return true;
    }

    return false;
}

bool SsrfGuard::isPrivateIp(const Poco::Net::IPAddress &addr, bool allowLoopback)
{
    if (addr.isLoopback())
    {
        return !allowLoopback;
    }

    // wildcard (0.0.0.0 or ::), link-local (169.254/16, fe80::/10), site-local (10/8, 172.16/12, 192.168/16, fc00::/7)
    if (addr.isWildcard() || addr.isLinkLocal() || addr.isSiteLocal() || addr.isMulticast())
    {
        return true;
    }

    const auto *bytes = reinterpret_cast<const unsigned char *>(addr.addr());
    auto length = addr.length();

    // unwrap an ipv4-mapped or ipv4-compatible ipv6 address (::ffff:a.b.c.d) to its embedded ipv4
    if (length == 16)
    {
        bool leadingZeros = true;

        for (int i = 0; i < 10; ++i)
        {
            if (bytes[i] != 0)
            {
                leadingZeros = false;
                break;
            }
        }

        bool mapped = leadingZeros && bytes[10] == 0xff && bytes[11] == 0xff;
        bool compatible = leadingZeros && bytes[10] == 0 && bytes[11] == 0;

        if (mapped || compatible)
        {
            bytes += 12;
            length = 4;
        }
    }

    if (length == 4)
    {
        return isBlockedIpv4(bytes, allowLoopback);
    }

    return false;
}

void SsrfGuard::validateUrl(const std::string &url)
{
    validateUrlImpl(url, false);
}

void SsrfGuard::validatePeerAddress(const Poco::Net::IPAddress &addr, bool allowLoopback)
{
    // recheck the actually connected ip so a dns rebind between validation and connect cannot reach a private host
    if (isPrivateIp(addr, allowLoopback))
    {
        throw std::runtime_error("[SsrfGuard] Connection resolved to a private IP address: " + addr.toString());
    }
}

void SsrfGuard::validateUrlAllowLoopback(const std::string &url)
{
    validateUrlImpl(url, true);
}

void SsrfGuard::validateUrlImpl(const std::string &url, bool allowLoopback)
{
    Poco::URI uri;

    try
    {
        uri = Poco::URI(url);
    }
    catch (const std::exception &)
    {
        throw std::runtime_error("[SsrfGuard] Invalid URL: " + url);
    }

    // validate scheme
    auto scheme = uri.getScheme();

    if (scheme != "http" && scheme != "https")
    {
        throw std::runtime_error("[SsrfGuard] Only http and https URLs are allowed, got: " + scheme);
    }

    auto host = uri.getHost();

    if (host.empty())
    {
        throw std::runtime_error("[SsrfGuard] URL has no host: " + url);
    }

    // resolve DNS and check for private IPs
    try
    {
        auto addresses = Poco::Net::DNS::hostByName(host).addresses();

        for (const auto &addr : addresses)
        {
            if (isPrivateIp(addr, allowLoopback))
            {
                throw std::runtime_error("[SsrfGuard] URL resolves to a private IP address: " + host + " -> " + addr.toString());
            }
        }
    }
    catch (const Poco::Net::HostNotFoundException &)
    {
        throw std::runtime_error("[SsrfGuard] Cannot resolve host: " + host);
    }
    catch (const std::runtime_error &)
    {
        throw; // re-throw our own errors
    }
    catch (const std::exception &e)
    {
        throw std::runtime_error("[SsrfGuard] DNS resolution failed for " + host + ": " + e.what());
    }
}

} // namespace util
} // namespace ionclaw
