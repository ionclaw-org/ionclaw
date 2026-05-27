#include "ionclaw/tool/builtin/EnvironmentTool.hpp"

#include <sstream>

#include "ionclaw/util/EnvironmentHelper.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

ToolResult EnvironmentTool::execute(const nlohmann::json &params, const ToolContext &context)
{
    auto action = params.value("action", std::string("list"));

    // report whether a single variable is set, without ever revealing its value
    if (action == "has_var")
    {
        auto name = params.value("name", std::string());

        if (name.empty())
        {
            return "Error: 'name' is required for the has_var action";
        }

        bool exists = ionclaw::util::EnvironmentHelper::isSet(name);
        return name + (exists ? " is set." : " is not set.");
    }

    if (action == "list")
    {
        if (context.projectPath.empty())
        {
            return "Error: project path not available";
        }

        auto vars = ionclaw::util::EnvironmentHelper::readDotEnv(context.projectPath);

        if (vars.empty())
        {
            return "No environment variables are defined in the project .env.";
        }

        std::ostringstream output;
        output << "Environment variables available for ${NAME} substitution (values hidden):\n";

        for (const auto &entry : vars)
        {
            output << "- " << entry.first << "\n";
        }

        output << "\nReference a value as ${NAME} in http_client or web_fetch (url, headers, or body). "
                  "The server substitutes the real value at request time, so the secret never appears here.";

        return output.str();
    }

    return "Error: unknown action '" + action + "'";
}

ToolSchema EnvironmentTool::schema() const
{
    return {
        "environment",
        "Inspect the project environment variables. Values are never returned. "
        "action 'list' returns the variable names defined in the project .env. "
        "action 'has_var' reports whether the variable in 'name' is set. "
        "To use a value, reference it as ${NAME} in http_client or web_fetch and the server substitutes the real value at request time.",
        {{"type", "object"},
         {"properties",
          {{"action", {{"type", "string"}, {"enum", nlohmann::json::array({"list", "has_var"})}, {"description", "What to inspect: 'list' (default) or 'has_var'"}}},
           {"name", {{"type", "string"}, {"description", "Variable name to check, required for the has_var action"}}}}}}};
}

} // namespace builtin
} // namespace tool
} // namespace ionclaw
