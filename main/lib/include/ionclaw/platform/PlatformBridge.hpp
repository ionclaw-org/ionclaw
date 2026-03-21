#pragma once

#include <functional>
#include <mutex>
#include <string>

#include "nlohmann/json.hpp"

namespace ionclaw
{
namespace platform
{

class PlatformBridge
{
public:
    using Handler = std::function<std::string(const std::string &function, const nlohmann::json &params)>;

    static PlatformBridge &instance();

    void setHandler(Handler handler);
    std::string invoke(const std::string &function, const nlohmann::json &params);

private:
    PlatformBridge();

    // returns the current platform name for error messages
    static std::string platformName();

    Handler handler;
    mutable std::mutex mutex;
};

} // namespace platform
} // namespace ionclaw
