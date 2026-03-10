#pragma once

#include <regex>
#include <vector>

#include "ionclaw/tool/Tool.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

class ExecTool : public Tool
{
public:
    ToolResult execute(const nlohmann::json &params, const ToolContext &context) override;
    ToolSchema schema() const override;
    std::set<std::string> supportedPlatforms() const override;

private:
    // constants
    static const size_t MAX_OUTPUT_BYTES;
    static const std::vector<std::regex> DANGEROUS_PATTERNS;

    // helpers
    static std::string validateCommand(const std::string &command);
};

} // namespace builtin
} // namespace tool
} // namespace ionclaw
