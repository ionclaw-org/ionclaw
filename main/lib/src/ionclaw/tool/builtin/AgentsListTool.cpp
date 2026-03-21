#include "ionclaw/tool/builtin/AgentsListTool.hpp"

#include <sstream>

#include "ionclaw/config/Config.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

ToolResult AgentsListTool::execute(const nlohmann::json &, const ToolContext &context)
{
    if (!context.config)
    {
        return "Error: config not available";
    }

    const auto &agents = context.config->agents;

    if (agents.empty())
    {
        return "No agents configured.";
    }

    std::ostringstream output;
    output << "Available agents:\n";

    for (const auto &[name, cfg] : agents)
    {
        output << "- " << name;

        if (!cfg.description.empty())
        {
            output << ": " << cfg.description;
        }

        if (!cfg.model.empty())
        {
            output << " (model: " << cfg.model << ")";
        }

        output << "\n";
    }

    return output.str();
}

ToolSchema AgentsListTool::schema() const
{
    return {
        "agents_list",
        "List all configured agents with their names, descriptions, and models.",
        {{"type", "object"},
         {"properties", nlohmann::json::object()}}};
}

} // namespace builtin
} // namespace tool
} // namespace ionclaw
