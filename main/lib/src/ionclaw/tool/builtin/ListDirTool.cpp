#include "ionclaw/tool/builtin/ListDirTool.hpp"

#include <filesystem>
#include <sstream>

#include "ionclaw/tool/builtin/ToolHelper.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

ToolResult ListDirTool::execute(const nlohmann::json &params, const ToolContext &context)
{
    auto rawPath = params.at("path").get<std::string>();
    auto resolvedPath = ToolHelper::validateAndResolvePath(context.workspacePath, rawPath, context.publicPath);

    if (!std::filesystem::exists(resolvedPath))
    {
        return "Error: path not found: " + rawPath;
    }

    if (!std::filesystem::is_directory(resolvedPath))
    {
        return "Error: not a directory: " + rawPath;
    }

    std::ostringstream result;

    for (const auto &entry : std::filesystem::directory_iterator(resolvedPath))
    {
        auto name = entry.path().filename().string();
        auto type = entry.is_directory() ? "dir" : "file";

        if (entry.is_regular_file())
        {
            auto size = entry.file_size();
            result << "[" << type << "] " << name << " (" << size << " bytes)\n";
        }
        else
        {
            result << "[" << type << "] " << name << "\n";
        }
    }

    auto output = result.str();

    if (output.empty())
    {
        return "Directory is empty: " + rawPath;
    }

    return ToolHelper::truncateOutput(output);
}

ToolSchema ListDirTool::schema() const
{
    return {
        "list_dir",
        "List the contents of a directory within the workspace, showing file types and sizes.",
        {{"type", "object"},
         {"properties", {{"path", {{"type", "string"}, {"description", "The directory path to list (absolute or relative to workspace)"}}}}},
         {"required", nlohmann::json::array({"path"})}}};
}

} // namespace builtin
} // namespace tool
} // namespace ionclaw
