#pragma once

#include "ionclaw/tool/Tool.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

class ImageOpsTool : public Tool
{
public:
    std::string execute(const nlohmann::json &params, const ToolContext &context) override;
    ToolSchema schema() const override;

private:
    static bool parseHexColor(const std::string &hex, unsigned char &r, unsigned char &g, unsigned char &b);
};

} // namespace builtin
} // namespace tool
} // namespace ionclaw
