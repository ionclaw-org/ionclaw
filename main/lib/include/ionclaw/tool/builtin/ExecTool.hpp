#pragma once

#include "ionclaw/tool/Tool.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

class ExecTool final : public Tool
{
public:
    ToolResult execute(const nlohmann::json &params, const ToolContext &context) override;
    ToolSchema schema() const override;
    std::set<std::string> supportedPlatforms() const override;

private:
    static const size_t MAX_OUTPUT_BYTES;
    static std::string validateCommand(const std::string &command);
};

} // namespace builtin
} // namespace tool
} // namespace ionclaw
