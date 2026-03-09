#include "ionclaw/tool/builtin/InvokePlatformTool.hpp"

#include "ionclaw/platform/PlatformBridge.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

std::string InvokePlatformTool::execute(const nlohmann::json &params, const ToolContext &context)
{
    auto function = params.at("function").get<std::string>();

    nlohmann::json functionParams = nlohmann::json::object();

    if (params.contains("params") && params["params"].is_object())
    {
        functionParams = params["params"];
    }

    return platform::PlatformBridge::instance().invoke(function, functionParams);
}

ToolSchema InvokePlatformTool::schema() const
{
    return {
        "invoke_platform",
        "Invoke a platform-specific function on the host system. "
        "Available functions depend on the current platform (e.g. ios, android, macos, linux, windows). "
        "Check the platform skill for the list of supported functions.",
        {{"type", "object"},
         {"properties",
          {{"function", {{"type", "string"}, {"description", "The platform function to invoke (e.g. 'local-notification.send')"}}},
           {"params", {{"type", "object"}, {"description", "Parameters for the function as a JSON object"}}}}},
         {"required", nlohmann::json::array({"function"})}}};
}

} // namespace builtin
} // namespace tool
} // namespace ionclaw
