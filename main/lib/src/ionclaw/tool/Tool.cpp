#include "ionclaw/tool/Tool.hpp"

namespace ionclaw
{
namespace tool
{

nlohmann::json ToolSchema::toOpenAiFormat() const
{
    return {
        {"type", "function"},
        {"function", {{"name", name}, {"description", description}, {"parameters", parameters}}}};
}

std::set<std::string> Tool::supportedPlatforms() const
{
    return {}; // empty = all platforms
}

} // namespace tool
} // namespace ionclaw
