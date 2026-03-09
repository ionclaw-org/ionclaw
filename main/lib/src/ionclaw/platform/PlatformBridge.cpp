#include "ionclaw/platform/PlatformBridge.hpp"

#include "ionclaw/tool/Platform.hpp"
#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace platform
{

static std::string platformName()
{
    return tool::Platform::current();
}

PlatformBridge::PlatformBridge()
    : handler([](const std::string &function, const nlohmann::json & /*params*/) -> std::string
              {
                  spdlog::debug("[PlatformBridge] Function '{}' not implemented on {}", function, platformName());
                  return "Error: '" + function + "' is not implemented on " + platformName() + "."; })
{
}

PlatformBridge &PlatformBridge::instance()
{
    static PlatformBridge bridge;
    return bridge;
}

void PlatformBridge::setHandler(Handler h)
{
    std::lock_guard<std::mutex> lock(mutex);
    handler = std::move(h);
    spdlog::info("[PlatformBridge] Handler registered");
}

std::string PlatformBridge::invoke(const std::string &function, const nlohmann::json &params)
{
    std::lock_guard<std::mutex> lock(mutex);

    spdlog::debug("[PlatformBridge] Invoking: {}", function);

    try
    {
        return handler(function, params);
    }
    catch (const std::exception &e)
    {
        spdlog::error("[PlatformBridge] Handler failed for '{}': {}", function, e.what());
        return "Error: '" + function + "' failed: " + e.what();
    }
    catch (...)
    {
        spdlog::error("[PlatformBridge] Handler failed for '{}': unknown error", function);
        return "Error: '" + function + "' failed: unknown error";
    }
}

} // namespace platform
} // namespace ionclaw
