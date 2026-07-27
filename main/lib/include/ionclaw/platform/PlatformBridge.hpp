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
    void clearHandler();
    std::string invoke(const std::string &function, const nlohmann::json &params);

private:
    PlatformBridge();

    static Handler defaultHandler();
    static std::string platformName();

    Handler handler;
    mutable std::mutex mutex;
};

} // namespace platform
} // namespace ionclaw
