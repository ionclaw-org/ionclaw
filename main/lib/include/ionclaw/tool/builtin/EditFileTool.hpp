#pragma once

#include "ionclaw/tool/Tool.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

class EditFileTool final : public Tool
{
public:
    ToolResult execute(const nlohmann::json &params, const ToolContext &context) override;
    ToolSchema schema() const override;

private:
    // find the closest matching section when old_text is not found
    static std::string findClosestMatch(const std::string &content, const std::string &query);
};

} // namespace builtin
} // namespace tool
} // namespace ionclaw
