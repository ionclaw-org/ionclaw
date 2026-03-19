#include "ionclaw/server/Auth.hpp"

#include <random>
#include <sstream>
#include <stdexcept>

#include "spdlog/spdlog.h"

#include "ionclaw/util/JwtHelper.hpp"

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
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 15);
        std::ostringstream oss;
        const char hex[] = "0123456789abcdef";

        for (int i = 0; i < 64; ++i)
        {
            oss << hex[dis(gen)];
        }

        secret = oss.str();
        spdlog::warn("No server credential configured, using ephemeral JWT secret");
    }
}

void Auth::reload(const ionclaw::config::Config &config)
{
    std::lock_guard<std::mutex> lock(mutex_);

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
    std::lock_guard<std::mutex> lock(mutex_);

    if (username != validUsername || password != validPassword)
    {
        throw std::runtime_error("Invalid username or password");
    }

    nlohmann::json payload = {{"sub", username}};
    return ionclaw::util::JwtHelper::create(payload, secret);
}

bool Auth::verifyToken(const std::string &token) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return ionclaw::util::JwtHelper::isValid(token, secret);
}

std::string Auth::extractBearerToken(const std::string &authHeader)
{
    const std::string prefix = "Bearer ";

    if (authHeader.size() > prefix.size() &&
        authHeader.substr(0, prefix.size()) == prefix)
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

    // read-only access to public files via api (GET only)
    if (method == "GET")
    {
        static const std::string publicFilePrefix = "/api/files/public/";
        static const std::string publicDownloadPrefix = "/api/files/download/public/";

        if ((path.size() >= publicFilePrefix.size() &&
             path.compare(0, publicFilePrefix.size(), publicFilePrefix) == 0) ||
            (path.size() >= publicDownloadPrefix.size() &&
             path.compare(0, publicDownloadPrefix.size(), publicDownloadPrefix) == 0))
        {
            return true;
        }
    }

    return false;
}

} // namespace server
} // namespace ionclaw
