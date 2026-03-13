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

ToolResult MemoryReadTool::execute(const nlohmann::json &params, const ToolContext &context)
{
    auto file = params.at("file").get<std::string>();
    auto memoryDir = context.workspacePath + "/memory";

    // list available files when requested
    if (file == "list")
    {
        std::ostringstream output;
        std::error_code ec;

        for (const auto &entry : std::filesystem::directory_iterator(memoryDir, ec))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".md")
            {
                output << entry.path().filename().string() << "\n";
            }
        }

        auto result = output.str();
        return result.empty() ? "No memory files found." : result;
    }

    // resolve filename (accept with or without .md extension)
    auto filename = file;

    if (filename.size() < 3 || filename.substr(filename.size() - 3) != ".md")
    {
        filename += ".md";
    }

    // prevent path traversal
    if (filename.find('/') != std::string::npos || filename.find('\\') != std::string::npos ||
        filename.find("..") != std::string::npos)
    {
        return "Error: invalid filename";
    }

    auto filePath = memoryDir + "/" + filename;

    if (!std::filesystem::exists(filePath))
    {
        return filename + " does not exist.";
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
        "Read a memory file from the memory directory. Use 'list' to see available files.",
        {{"type", "object"},
         {"properties",
          {{"file",
            {{"type", "string"},
             {"description",
              "Filename to read (e.g. 'MEMORY.md', '2026-03-13.md') or 'list' to show available files"}}},
           {"max_lines", {{"type", "integer"}, {"description", "Return only the last N lines"}}}}},
         {"required", nlohmann::json::array({"file"})}}};
}

} // namespace builtin
} // namespace tool
} // namespace ionclaw
