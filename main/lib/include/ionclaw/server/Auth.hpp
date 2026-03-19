#pragma once

#include <mutex>
#include <string>

#include "nlohmann/json.hpp"

#include "ionclaw/config/Config.hpp"

namespace ionclaw
{
namespace server
{

class Auth
{
public:
    explicit Auth(const ionclaw::config::Config &config);
    void reload(const ionclaw::config::Config &config);

    std::string login(const std::string &username, const std::string &password);
    bool verifyToken(const std::string &token) const;

    static std::string extractBearerToken(const std::string &authHeader);
    static bool isPublicPath(const std::string &path, const std::string &method = "GET");

private:
    mutable std::mutex mutex_;
    std::string secret;
    std::string validUsername;
    std::string validPassword;
};

} // namespace server
} // namespace ionclaw
