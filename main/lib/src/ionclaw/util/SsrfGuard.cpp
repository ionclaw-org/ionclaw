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

namespace
{

// check if address is private/reserved (works for both IPv4 and IPv6)
bool isPrivateIp(const Poco::Net::IPAddress &addr, bool allowLoopback)
{
    if (addr.isLoopback())
    {
        return !allowLoopback;
    }

    // wildcard addresses (0.0.0.0 or ::)
    if (addr.isWildcard())
    {
        return true;
    }

    // link-local: 169.254.0.0/16 (IPv4), fe80::/10 (IPv6)
    if (addr.isLinkLocal())
    {
        return true;
    }

    // site-local/private: 10/8, 172.16/12, 192.168/16 (IPv4), fc00::/7 + fec0::/10 (IPv6)
    if (addr.isSiteLocal())
    {
        return true;
    }

    return false;
}

} // namespace

void SsrfGuard::validateUrl(const std::string &url)
{
    validateUrlImpl(url, false);
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
        throw std::runtime_error("Invalid URL: " + url);
    }

    // validate scheme
    auto scheme = uri.getScheme();

    if (scheme != "http" && scheme != "https")
    {
        throw std::runtime_error("Only http and https URLs are allowed, got: " + scheme);
    }

    auto host = uri.getHost();

    if (host.empty())
    {
        throw std::runtime_error("URL has no host: " + url);
    }

    // resolve DNS and check for private IPs
    try
    {
        auto addresses = Poco::Net::DNS::hostByName(host).addresses();

        for (const auto &addr : addresses)
        {
            if (isPrivateIp(addr, allowLoopback))
            {
                throw std::runtime_error("URL resolves to a private IP address: " + host + " -> " + addr.toString());
            }
        }
    }
    catch (const Poco::Net::HostNotFoundException &)
    {
        throw std::runtime_error("Cannot resolve host: " + host);
    }
    catch (const std::runtime_error &)
    {
        throw; // re-throw our own errors
    }
    catch (const std::exception &e)
    {
        throw std::runtime_error("DNS resolution failed for " + host + ": " + e.what());
    }
}

} // namespace util
} // namespace ionclaw
