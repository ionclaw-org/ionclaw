#pragma once

#include "ionclaw/tool/Tool.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

class ReadFileTool final : public Tool
{
public:
    ToolResult execute(const nlohmann::json &params, const ToolContext &context) override;
    ToolSchema schema() const override;

private:
    // constants
    static constexpr size_t MAX_READ_BYTES = 50 * 1024; // 50KB per read
};

} // namespace builtin
} // namespace tool
} // namespace ionclaw
