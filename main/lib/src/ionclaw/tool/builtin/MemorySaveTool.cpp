#include "ionclaw/tool/builtin/MemorySaveTool.hpp"

#include <filesystem>
#include <fstream>

namespace ionclaw
{
namespace tool
{
namespace builtin
{

ToolResult MemorySaveTool::execute(const nlohmann::json &params, const ToolContext &context)
{
    auto historyEntry = params.at("history_entry").get<std::string>();
    auto updatedMemory = params.at("updated_memory").get<std::string>();

    auto memoryDir = context.workspacePath + "/memory";
    std::error_code ec;
    std::filesystem::create_directories(memoryDir, ec);

    // append history entry to HISTORY.md
    auto historyPath = memoryDir + "/HISTORY.md";
    std::ofstream historyFile(historyPath, std::ios::app);

    if (!historyFile.is_open())
    {
        return "Error: cannot write to HISTORY.md";
    }

    historyFile << historyEntry << "\n\n";
    historyFile.close();

    // overwrite MEMORY.md with updated memory
    auto memoryPath = memoryDir + "/MEMORY.md";
    std::ofstream memoryFile(memoryPath, std::ios::trunc);

    if (!memoryFile.is_open())
    {
        return "Error: cannot write to MEMORY.md";
    }

    memoryFile << updatedMemory;
    memoryFile.close();

    return "Memory saved successfully.";
}

ToolSchema MemorySaveTool::schema() const
{
    return {
        "memory_save",
        "Save conversation history and updated memory. Appends to HISTORY.md and overwrites MEMORY.md.",
        {{"type", "object"},
         {"properties",
          {{"history_entry", {{"type", "string"}, {"description", "Summary of the conversation to append to HISTORY.md"}}},
           {"updated_memory", {{"type", "string"}, {"description", "Updated long-term memory content to save to MEMORY.md"}}}}},
         {"required", nlohmann::json::array({"history_entry", "updated_memory"})}}};
}

} // namespace builtin
} // namespace tool
} // namespace ionclaw
