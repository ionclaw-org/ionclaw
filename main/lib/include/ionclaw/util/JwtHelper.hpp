#pragma once

#include <string>

#include "nlohmann/json.hpp"

namespace ionclaw
{
namespace util
{

class JwtHelper
{
public:
    static std::string create(const nlohmann::json &payload, const std::string &secret, int expiryHours = 24);
    static nlohmann::json verify(const std::string &token, const std::string &secret);
    static bool isValid(const std::string &token, const std::string &secret);
};

} // namespace util
} // namespace ionclaw
