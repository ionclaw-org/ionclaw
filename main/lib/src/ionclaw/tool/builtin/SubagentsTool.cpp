#include "ionclaw/tool/builtin/SubagentsTool.hpp"

#include "ionclaw/agent/SubagentRegistry.hpp"
#include "ionclaw/util/StringHelper.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

ToolResult SubagentsTool::execute(const nlohmann::json &params, const ToolContext &context)
{
    if (!context.subagentRegistry)
    {
        return "Error: subagent registry not available";
    }

    auto action = params.value("action", "list");

    if (action == "list")
    {
        auto children = context.subagentRegistry->getChildren(context.sessionKey);

        if (children.empty())
        {
            return "No subagent runs found for this session.";
        }

        std::string result;

        for (const auto &record : children)
        {
            result += "- " + record.runId + " [" +
                      ionclaw::agent::SubagentRunRecord::statusToString(record.status) + "]";

            if (!record.task.empty())
            {
                auto taskPreview = ionclaw::util::StringHelper::utf8SafeTruncate(record.task, 80);

                if (record.task.size() > 80)
                {
                    taskPreview += "...";
                }

                result += " task: " + taskPreview;
            }

            if (!record.outcome.empty())
            {
                auto outcomePreview = ionclaw::util::StringHelper::utf8SafeTruncate(record.outcome, 200);

                if (record.outcome.size() > 200)
                {
                    outcomePreview += "...";
                }

                result += "\n  result: " + outcomePreview;
            }
            else if (!record.lastOutput.empty())
            {
                result += "\n  progress: " + record.lastOutput;
            }

            result += "\n";
        }

        return result;
    }
    else if (action == "kill")
    {
        if (!params.contains("run_id") || !params["run_id"].is_string())
        {
            return "Error: run_id is required for kill action";
        }

        auto runId = params["run_id"].get<std::string>();
        bool cascade = params.value("cascade", true);
        int killed = context.subagentRegistry->killRun(runId, cascade);

        if (killed == 0)
        {
            return "No active runs found to kill for run_id: " + runId;
        }

        return "Killed " + std::to_string(killed) + " run(s)" +
               (cascade ? " (including descendants)" : "");
    }
    else if (action == "status")
    {
        if (!params.contains("run_id") || !params["run_id"].is_string())
        {
            return "Error: run_id is required for status action";
        }

        auto runId = params["run_id"].get<std::string>();
        auto record = context.subagentRegistry->getRecord(runId);

        if (record.runId.empty())
        {
            return "Error: run not found: " + runId;
        }

        auto j = record.toJson();
        return j.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    return "Error: unknown action '" + action + "'. Valid actions: list, kill, status";
}

ToolSchema SubagentsTool::schema() const
{
    return {
        "subagents",
        "Manage spawned subagent tasks. List active runs, check status, or kill a run.",
        {{"type", "object"},
         {"properties",
          {{"action", {{"type", "string"}, {"enum", {"list", "kill", "status"}}, {"description", "Action to perform: list, kill, or status"}}},
           {"run_id", {{"type", "string"}, {"description", "The run ID (required for kill and status actions)"}}},
           {"cascade", {{"type", "boolean"}, {"description", "When killing, also kill all descendant runs (default: true)"}}}}},
         {"required", nlohmann::json::array({"action"})}}};
}

std::set<std::string> SubagentsTool::supportedPlatforms() const
{
    return {"linux", "macos", "windows"};
}

} // namespace builtin
} // namespace tool
} // namespace ionclaw
