#pragma once

#include "ionclaw/tool/Tool.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

class VisionTool final : public Tool
{
public:
    ToolResult execute(const nlohmann::json &params, const ToolContext &context) override;
    ToolSchema schema() const override;

private:
    static std::string normalizeExtension(const std::string &pathOrUrl);
    static std::string detectMimeType(const std::string &pathOrUrl);
    static std::string detectMimeFromContentType(const std::string &contentType);
    static void stbWriteToVector(void *context, void *data, int size);
};

} // namespace builtin
} // namespace tool
} // namespace ionclaw
