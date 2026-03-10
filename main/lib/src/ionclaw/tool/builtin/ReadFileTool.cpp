#include "ionclaw/tool/builtin/ReadFileTool.hpp"

#include <filesystem>
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
    auto resolvedPath = ToolHelper::validateAndResolvePath(context.workspacePath, rawPath, context.publicPath);

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

    // read all lines
    std::vector<std::string> lines;
    std::string line;

    while (std::getline(file, line))
    {
        // remove trailing carriage return
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        lines.push_back(line);
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
    }

    // calculate range
    int startLine = offset - 1; // convert to 0-based
    int endLine = totalLines;

    if (limit > 0)
    {
        endLine = std::min(startLine + limit, totalLines);
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

    if (remaining > 0)
    {
        output << "\n[Showing lines " << (startLine + 1) << "-" << actualEnd
               << " of " << totalLines << ". " << remaining
               << " more lines remaining. Use offset=" << (actualEnd + 1)
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
