#include "ionclaw/tool/builtin/MemorySaveTool.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace ionclaw
{
namespace tool
{
namespace builtin
{

ToolResult MemorySaveTool::execute(const nlohmann::json &params, const ToolContext &context)
{
    auto content = params.at("content").get<std::string>();

    auto memoryDir = context.workspacePath + "/memory";
    std::error_code ec;
    std::filesystem::create_directories(memoryDir, ec);

    // build today's daily log filename (YYYY-MM-DD.md)
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = {};
#if defined(_WIN32)
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif

    std::ostringstream filename;
    filename << std::put_time(&tm, "%Y-%m-%d") << ".md";

    auto filePath = memoryDir + "/" + filename.str();

    // check if file already has content (before opening for append)
    bool hasContent = false;
    auto fileSize = std::filesystem::file_size(filePath, ec);

    if (!ec && fileSize > 0)
    {
        hasContent = true;
    }

    // append to daily log
    std::ofstream file(filePath, std::ios::app);

    if (!file.is_open())
    {
        return "Error: cannot write to " + filename.str();
    }

    // add separator between entries
    if (hasContent)
    {
        file << "\n";
    }

    file << content << "\n";
    file.close();

    return "Saved to " + filename.str();
}

ToolSchema MemorySaveTool::schema() const
{
    return {
        "memory_save",
        "Append a memory entry to today's daily log (memory/YYYY-MM-DD.md). "
        "Use for durable facts, conversation summaries, and important context.",
        {{"type", "object"},
         {"properties",
          {{"content", {{"type", "string"}, {"description", "Content to append to today's daily log"}}}}},
         {"required", nlohmann::json::array({"content"})}}};
}

} // namespace builtin
} // namespace tool
} // namespace ionclaw
