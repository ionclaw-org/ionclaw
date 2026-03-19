#pragma once

#include "ionclaw/tool/Tool.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

class WebSearchTool final : public Tool
{
public:
    ToolResult execute(const nlohmann::json &params, const ToolContext &context) override;
    ToolSchema schema() const override;
};

} // namespace builtin
} // namespace tool
} // namespace ionclaw
