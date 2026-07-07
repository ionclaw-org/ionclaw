#include "ionclaw/tool/builtin/WriteFileTool.hpp"

#include <filesystem>
#include <fstream>

#include "ionclaw/config/Config.hpp"
#include "ionclaw/tool/builtin/ToolHelper.hpp"
#include "ionclaw/util/FileHelper.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

ToolResult WriteFileTool::execute(const nlohmann::json &params, const ToolContext &context)
{
    auto rawPath = params.at("path").get<std::string>();
    auto content = params.at("content").get<std::string>();
    bool restrict = !context.config || context.config->tools.restrictToWorkspace;
    auto resolvedPath = ToolHelper::validateAndResolvePath(context.projectPath, context.workspacePath, rawPath, context.publicPath, restrict);

    auto parentDir = std::filesystem::path(resolvedPath).parent_path();

    if (!parentDir.empty() && !std::filesystem::exists(parentDir))
    {
        std::error_code ec;
        std::filesystem::create_directories(parentDir, ec);

        if (ec)
        {
            return "Error: cannot create directory (" + ec.message() + "): " + parentDir.string();
        }
    }

    // atomic write via a unique temp file then rename, so concurrent writers never share a temp and a crash never leaves a torn file
    auto writeError = ionclaw::util::FileHelper::atomicWrite(resolvedPath, content);

    if (!writeError.empty())
    {
        return "Error: " + writeError;
    }

    return "File written successfully: " + rawPath;
}

ToolSchema WriteFileTool::schema() const
{
    return {
        "write_file",
        "Write content to a file at the given path within the workspace. Creates parent directories if needed.",
        {{"type", "object"},
         {"properties", {{"path", {{"type", "string"}, {"description", "The file path to write (absolute or relative to workspace)"}}}, {"content", {{"type", "string"}, {"description", "The content to write to the file"}}}}},
         {"required", nlohmann::json::array({"path", "content"})}}};
}

} // namespace builtin
} // namespace tool
} // namespace ionclaw
