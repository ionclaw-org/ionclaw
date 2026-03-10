#pragma once

#include "ionclaw/tool/Tool.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

class WebFetchTool : public Tool
{
public:
    ToolResult execute(const nlohmann::json &params, const ToolContext &context) override;
    ToolSchema schema() const override;

private:
    static std::string stripHtml(const std::string &html);
};

} // namespace builtin
} // namespace tool
} // namespace ionclaw
