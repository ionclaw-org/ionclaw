#include "ionclaw/tool/builtin/WriteFileTool.hpp"

#include <filesystem>
#include <fstream>

#include "ionclaw/config/Config.hpp"
#include "ionclaw/tool/builtin/ToolHelper.hpp"

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

    // atomic write: write to temp file then rename to prevent data loss on crash
    auto tempPath = resolvedPath + ".tmp";
    std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);

    if (!file.is_open())
    {
        return "Error: cannot write to file: " + rawPath;
    }

    file << content;
    file.flush();

    if (file.fail())
    {
        std::filesystem::remove(tempPath);
        return "Error: write failed (disk full or I/O error): " + rawPath;
    }

    file.close();

    std::error_code renameEc;
    std::filesystem::rename(tempPath, resolvedPath, renameEc);

    if (renameEc)
    {
        std::filesystem::remove(tempPath);
        return "Error: failed to finalize file write: " + renameEc.message();
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
