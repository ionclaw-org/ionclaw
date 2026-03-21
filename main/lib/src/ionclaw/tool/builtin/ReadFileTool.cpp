#include "ionclaw/tool/builtin/ReadFileTool.hpp"

#include <filesystem>

#include "ionclaw/config/Config.hpp"
#include <fstream>
#include <sstream>
#include <vector>

#include "ionclaw/tool/builtin/ToolHelper.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

// execute file read
ToolResult ReadFileTool::execute(const nlohmann::json &params, const ToolContext &context)
{
    auto rawPath = params.at("path").get<std::string>();
    bool restrict = !context.config || context.config->tools.restrictToWorkspace;
    auto resolvedPath = ToolHelper::validateAndResolvePath(context.projectPath, context.workspacePath, rawPath, context.publicPath, restrict);

    // validate file existence
    if (!std::filesystem::exists(resolvedPath))
    {
        return "Error: file not found: " + rawPath;
    }

    if (!std::filesystem::is_regular_file(resolvedPath))
    {
        return "Error: not a regular file: " + rawPath;
    }

    std::ifstream file(resolvedPath, std::ios::binary);

    if (!file.is_open())
    {
        return "Error: cannot open file: " + rawPath;
    }

    // read lines with a cap to prevent unbounded memory on huge files
    constexpr size_t MAX_LINES = 100000;
    std::vector<std::string> lines;
    std::string line;
    bool linesCapped = false;

    while (std::getline(file, line))
    {
        // remove trailing carriage return
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        lines.push_back(std::move(line));

        if (lines.size() >= MAX_LINES)
        {
            linesCapped = true;
            break;
        }
    }

    file.close();

    auto totalLines = static_cast<int>(lines.size());

    if (totalLines == 0)
    {
        return "(empty file)";
    }

    // parse offset and limit
    int offset = 1; // 1-based
    int limit = 0;  // 0 = no limit

    if (params.contains("offset") && params["offset"].is_number_integer())
    {
        offset = params["offset"].get<int>();

        if (offset < 1)
        {
            offset = 1;
        }

        if (offset > totalLines)
        {
            return "Error: offset " + std::to_string(offset) + " exceeds total lines (" + std::to_string(totalLines) + ")";
        }
    }

    if (params.contains("limit") && params["limit"].is_number_integer())
    {
        limit = params["limit"].get<int>();

        if (limit < 1)
        {
            limit = 0;
        }
        else if (limit > totalLines)
        {
            limit = totalLines;
        }
    }

    // calculate range
    int startLine = offset - 1; // convert to 0-based
    int endLine = totalLines;

    if (limit > 0)
    {
        // clamp limit to remaining lines to prevent integer overflow
        int remaining = totalLines - startLine;
        endLine = startLine + std::min(limit, remaining);
    }

    // build output with line numbers, respecting size limit
    std::ostringstream output;
    size_t bytesWritten = 0;
    int actualEnd = startLine;

    for (int i = startLine; i < endLine; ++i)
    {
        auto lineStr = std::to_string(i + 1) + ": " + lines[i] + "\n";

        if (bytesWritten + lineStr.size() > MAX_READ_BYTES && i > startLine)
        {
            break;
        }

        output << lineStr;
        bytesWritten += lineStr.size();
        actualEnd = i + 1;
    }

    // add continuation hint if there are more lines
    int remaining = totalLines - actualEnd;

    if (remaining > 0 || linesCapped)
    {
        output << "\n[Showing lines " << (startLine + 1) << "-" << actualEnd
               << " of " << (linesCapped ? std::to_string(totalLines) + "+" : std::to_string(totalLines))
               << ". " << (linesCapped ? "File truncated at " + std::to_string(MAX_LINES) + " lines. " : "")
               << remaining << " more lines available. Use offset=" << (actualEnd + 1)
               << " to continue.]";
    }

    return output.str();
}

// schema definition
ToolSchema ReadFileTool::schema() const
{
    return {
        "read_file",
        "Read the contents of a file at the given path within the workspace. Supports pagination with offset and limit parameters.",
        {{"type", "object"},
         {"properties",
          {{"path", {{"type", "string"}, {"description", "The file path to read (absolute or relative to workspace)"}}},
           {"offset", {{"type", "integer"}, {"description", "Start reading from this line number (1-based). Default: 1"}}},
           {"limit", {{"type", "integer"}, {"description", "Maximum number of lines to read. Default: read all"}}}}},
         {"required", nlohmann::json::array({"path"})}}};
}

} // namespace builtin
} // namespace tool
} // namespace ionclaw
