#include "ionclaw/tool/builtin/MemoryReadTool.hpp"

#include <deque>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace ionclaw
{
namespace tool
{
namespace builtin
{

std::string MemoryReadTool::execute(const nlohmann::json &params, const ToolContext &context)
{
    auto file = params.at("file").get<std::string>();

    std::string filename;

    if (file == "memory")
    {
        filename = "MEMORY.md";
    }
    else if (file == "history")
    {
        filename = "HISTORY.md";
    }
    else
    {
        return "Error: file must be 'memory' or 'history'";
    }

    auto filePath = context.workspacePath + "/memory/" + filename;

    if (!std::filesystem::exists(filePath))
    {
        return filename + " does not exist yet.";
    }

    std::ifstream inFile(filePath, std::ios::binary);

    if (!inFile.is_open())
    {
        return "Error: cannot read " + filename;
    }

    // check if max_lines is specified for tail reading
    if (params.contains("max_lines") && params["max_lines"].is_number_integer())
    {
        auto maxLines = params["max_lines"].get<int>();

        if (maxLines < 1)
        {
            maxLines = 1;
        }

        // read all lines, keep last N
        std::deque<std::string> lines;
        std::string line;

        while (std::getline(inFile, line))
        {
            lines.push_back(line);

            if (static_cast<int>(lines.size()) > maxLines)
            {
                lines.pop_front();
            }
        }

        std::ostringstream output;

        for (const auto &l : lines)
        {
            output << l << "\n";
        }

        return output.str();
    }

    // read entire file
    std::ostringstream content;
    content << inFile.rdbuf();
    return content.str();
}

ToolSchema MemoryReadTool::schema() const
{
    return {
        "memory_read",
        "Read memory files. 'memory' reads long-term facts (MEMORY.md), 'history' reads conversation summaries (HISTORY.md).",
        {{"type", "object"},
         {"properties",
          {{"file", {{"type", "string"}, {"enum", nlohmann::json::array({"memory", "history"})}, {"description", "Which memory file to read"}}},
           {"max_lines", {{"type", "integer"}, {"description", "Return only the last N lines"}}}}},
         {"required", nlohmann::json::array({"file"})}}};
}

} // namespace builtin
} // namespace tool
} // namespace ionclaw
