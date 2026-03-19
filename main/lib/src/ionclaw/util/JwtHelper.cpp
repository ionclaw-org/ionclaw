#include "ionclaw/util/JwtHelper.hpp"

#ifdef IONCLAW_HAS_SSL

#include <chrono>

#include "jwt-cpp/traits/nlohmann-json/traits.h"

namespace ionclaw
{
namespace util
{

using traits = jwt::traits::nlohmann_json;

std::string JwtHelper::create(const nlohmann::json &payload, const std::string &secret, int expiryHours)
{
    auto now = std::chrono::system_clock::now();
    auto exp = now + std::chrono::hours(expiryHours);

    auto builder = jwt::create<traits>()
                       .set_type("JWT")
                       .set_issued_at(now)
                       .set_expires_at(exp);

    if (payload.contains("sub"))
    {
        builder.set_subject(payload["sub"].get<std::string>());
    }

    for (auto it = payload.begin(); it != payload.end(); ++it)
    {
        if (it.key() == "sub" || it.key() == "iat" || it.key() == "exp")
        {
            continue;
        }

        builder.set_payload_claim(it.key(), it.value());
    }

    return builder.sign(jwt::algorithm::hs256{secret});
}

nlohmann::json JwtHelper::verify(const std::string &token, const std::string &secret)
{
    auto verifier = jwt::verify<traits>()
                        .allow_algorithm(jwt::algorithm::hs256{secret});

    auto decoded = jwt::decode<traits>(token);
    verifier.verify(decoded);

    nlohmann::json result;

    for (auto &claim : decoded.get_payload_json())
    {
        result[claim.first] = claim.second;
    }

    return result;
}

bool JwtHelper::isValid(const std::string &token, const std::string &secret)
{
    try
    {
        verify(token, secret);
        return true;
    }
    catch (const std::exception &)
    {
        return false;
    }
}

} // namespace util
} // namespace ionclaw

#else

#include <stdexcept>

namespace ionclaw
{
namespace util
{

std::string JwtHelper::create(const nlohmann::json &, const std::string &, int)
{
    throw std::runtime_error("JWT is not supported (built without SSL)");
}

nlohmann::json JwtHelper::verify(const std::string &, const std::string &)
{
    throw std::runtime_error("JWT is not supported (built without SSL)");
}

bool JwtHelper::isValid(const std::string &, const std::string &)
{
    return false;
}

} // namespace util
} // namespace ionclaw

#endif
