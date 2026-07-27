#include "ionclaw/server/Auth.hpp"

#include <stdexcept>

#include "spdlog/spdlog.h"

#include "ionclaw/util/JwtHelper.hpp"
#include "ionclaw/util/RandomHelper.hpp"

namespace ionclaw
{
namespace server
{

Auth::Auth(const ionclaw::config::Config &config)
{
    // resolve web_client credential for username/password
    auto webCredIt = config.credentials.find(config.webClient.credential);

    if (webCredIt != config.credentials.end())
    {
        validUsername = webCredIt->second.username;
        validPassword = webCredIt->second.password;
    }

    // resolve server credential for JWT secret
    auto serverCredIt = config.credentials.find(config.server.credential);

    if (serverCredIt != config.credentials.end())
    {
        secret = serverCredIt->second.key;
    }

    // fallback defaults for direct construction without startup flow
    if (validUsername.empty())
    {
        validUsername = "admin";
    }

    if (validPassword.empty())
    {
        validPassword = "admin";
    }

    // generate ephemeral jwt secret if none configured
    if (secret.empty())
    {
        secret = ionclaw::util::RandomHelper::secureHex(32);
        spdlog::warn("[Auth] No server credential configured, using ephemeral JWT secret");
    }
}

void Auth::reload(const ionclaw::config::Config &config)
{
    std::lock_guard<std::mutex> lock(mutex);

    auto webCredIt = config.credentials.find(config.webClient.credential);

    if (webCredIt != config.credentials.end())
    {
        validUsername = webCredIt->second.username;
        validPassword = webCredIt->second.password;
    }

    auto serverCredIt = config.credentials.find(config.server.credential);

    if (serverCredIt != config.credentials.end() && !serverCredIt->second.key.empty())
    {
        secret = serverCredIt->second.key;
    }

    spdlog::info("[Auth] Credentials reloaded");
}

std::string Auth::login(const std::string &username, const std::string &password)
{
    std::lock_guard<std::mutex> lock(mutex);

    // evaluate both comparisons in constant time so timing cannot reveal the username or password
    bool userMatches = ionclaw::util::RandomHelper::constantTimeEquals(username, validUsername);
    bool passMatches = ionclaw::util::RandomHelper::constantTimeEquals(password, validPassword);

    if (!userMatches || !passMatches)
    {
        throw std::runtime_error("[Auth] Invalid username or password");
    }

    nlohmann::json payload = {{"sub", username}};
    return ionclaw::util::JwtHelper::create(payload, secret);
}

bool Auth::verifyToken(const std::string &token) const
{
    std::lock_guard<std::mutex> lock(mutex);
    return ionclaw::util::JwtHelper::isValid(token, secret);
}

std::string Auth::extractBearerToken(const std::string &authHeader)
{
    const std::string prefix = "Bearer ";

    if (authHeader.size() > prefix.size() && authHeader.substr(0, prefix.size()) == prefix)
    {
        return authHeader.substr(prefix.size());
    }

    return "";
}

bool Auth::isPublicPath(const std::string &path, const std::string &method)
{
    // login and health endpoints are always public
    if (path == "/api/auth/login" || path == "/api/health")
    {
        return true;
    }

    // static asset paths are public
    if (path.substr(0, 5) == "/app/" || path.substr(0, 8) == "/public/")
    {
        return true;
    }

    // inbound channel webhooks authenticate via provider signature/token, not the app session
    if (path.substr(0, 9) == "/webhook/")
    {
        return true;
    }

    // read-only access to public files via api (GET only)
    if (method == "GET")
    {
        static const std::string publicFilePrefix = "/api/files/public/";
        static const std::string publicDownloadPrefix = "/api/files/download/public/";

        if ((path.size() >= publicFilePrefix.size() && path.compare(0, publicFilePrefix.size(), publicFilePrefix) == 0) || (path.size() >= publicDownloadPrefix.size() && path.compare(0, publicDownloadPrefix.size(), publicDownloadPrefix) == 0))
        {
            return true;
        }
    }

    return false;
}

} // namespace server
} // namespace ionclaw
