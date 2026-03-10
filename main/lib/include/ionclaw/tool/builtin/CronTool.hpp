#pragma once

#include "ionclaw/tool/Tool.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

class CronTool : public Tool
{
public:
    ToolResult execute(const nlohmann::json &params, const ToolContext &context) override;
    ToolSchema schema() const override;
    std::set<std::string> supportedPlatforms() const override;
};

} // namespace builtin
} // namespace tool
} // namespace ionclaw
