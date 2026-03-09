#include "ionclaw/tool/builtin/MemorySearchTool.hpp"

#include <iomanip>
#include <sstream>

#include "ionclaw/agent/MemoryStore.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

std::string MemorySearchTool::execute(const nlohmann::json &params, const ToolContext &context)
{
    auto query = params.at("query").get<std::string>();

    ionclaw::agent::MemoryStore store(context.workspacePath);
    auto results = store.searchMemory(query);

    if (results.empty())
    {
        return "No matches found.";
    }

    std::ostringstream output;

    for (const auto &result : results)
    {
        output << "[" << result.file << ":" << result.line << "] "
               << "(score: " << std::fixed << std::setprecision(2) << result.score << ")\n"
               << result.context << "\n\n";
    }

    return output.str();
}

ToolSchema MemorySearchTool::schema() const
{
    return {
        "memory_search",
        "Search through all memory files for matching content using keyword extraction and relevance scoring.",
        {{"type", "object"},
         {"properties",
          {{"query", {{"type", "string"}, {"description", "The search query (keywords extracted automatically)"}}}}},
         {"required", nlohmann::json::array({"query"})}}};
}

} // namespace builtin
} // namespace tool
} // namespace ionclaw
