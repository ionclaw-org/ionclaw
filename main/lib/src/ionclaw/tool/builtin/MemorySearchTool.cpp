#include "ionclaw/tool/builtin/MemorySearchTool.hpp"

#include <iomanip>
#include <sstream>

#include "ionclaw/agent/MemoryStore.hpp"
#include "ionclaw/agent/SemanticMemory.hpp"
#include "ionclaw/config/Config.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

ToolResult MemorySearchTool::execute(const nlohmann::json &params, const ToolContext &context)
{
    auto query = params.at("query").get<std::string>();

    // semantic search takes over when an embeddings model is configured and its provider is available
    if (context.config)
    {
        ionclaw::agent::SemanticMemory semantic(context.workspacePath, *context.config);

        if (semantic.available())
        {
            auto results = semantic.search(query, 10);

            if (results.empty())
            {
                return "No matches found.";
            }

            std::ostringstream output;

            for (const auto &result : results)
            {
                output << "[" << result.file << "] "
                       << "(score: " << std::fixed << std::setprecision(2) << result.score << ")\n"
                       << result.text << "\n\n";
            }

            return output.str();
        }
    }

    // keyword search is the base capability used when no embeddings model is configured
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
        "Search through all memory files for content relevant to the query, ranked by relevance.",
        {{"type", "object"},
         {"properties",
          {{"query", {{"type", "string"}, {"description", "The search query"}}}}},
         {"required", nlohmann::json::array({"query"})}}};
}

} // namespace builtin
} // namespace tool
} // namespace ionclaw
