#pragma once

#include <set>
#include <string>

#include "ionclaw/tool/Tool.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

class GenerateImageTool : public Tool
{
public:
    std::string execute(const nlohmann::json &params, const ToolContext &context) override;
    ToolSchema schema() const override;

private:
    // constants
    static const std::set<std::string> VALID_ASPECT_RATIOS;
    static const std::set<std::string> VALID_SIZES;
};

} // namespace builtin
} // namespace tool
} // namespace ionclaw
