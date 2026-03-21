#include "ionclaw/tool/builtin/ExecTool.hpp"

#include <regex>
#include <vector>

#include "ionclaw/config/Config.hpp"
#include "ionclaw/tool/builtin/ToolHelper.hpp"
#include "ionclaw/util/ProcessRunner.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

// maximum output size before truncation
const size_t ExecTool::MAX_OUTPUT_BYTES = 200 * 1024; // 200KB

// validate command against dangerous patterns
// thread_local ensures thread-safe regex usage (libstdc++ data race on concurrent regex_search)
std::string ExecTool::validateCommand(const std::string &command)
{
    // check for path traversal
    if (command.find("../") != std::string::npos || command.find("..\\") != std::string::npos)
    {
        return "Command contains path traversal sequences (../ or ..\\)";
    }

    // thread_local to avoid data race on concurrent std::regex_search calls
    thread_local static const std::vector<std::regex> patterns = {
        // destructive file operations
        std::regex(R"(rm\s+(-\w*[rf]\w*\s+|.*--no-preserve-root))", std::regex::icase),
        std::regex(R"(del\s+/[fF])", std::regex::icase),
        std::regex(R"(rmdir\s+/[sS])", std::regex::icase),
        std::regex(R"(\bshred\b)", std::regex::icase),
        std::regex(R"(truncate\s+-s\s*0)", std::regex::icase),
        std::regex(R"(find\s+.*-delete)", std::regex::icase),

        // disk operations
        std::regex(R"(\bformat\s+[a-zA-Z]:)", std::regex::icase),
        std::regex(R"(\bmkfs\b)", std::regex::icase),
        std::regex(R"(\bdiskpart\b)", std::regex::icase),
        std::regex(R"(\bdd\s+if=)", std::regex::icase),

        // system operations
        std::regex(R"(\bshutdown\b)", std::regex::icase),
        std::regex(R"(\breboot\b)", std::regex::icase),
        std::regex(R"(\bpoweroff\b)", std::regex::icase),
        std::regex(R"(\bhalt\b)", std::regex::icase),

        // code injection
        std::regex(R"(\bbase64\s+-d\b)", std::regex::icase),
        std::regex(R"(\beval\s+)", std::regex::icase),
        std::regex(R"(curl\s+.*\|\s*sh)", std::regex::icase),
        std::regex(R"(curl\s+.*\|\s*bash)", std::regex::icase),
        std::regex(R"(wget\s+.*\|\s*sh)", std::regex::icase),
        std::regex(R"(wget\s+.*\|\s*bash)", std::regex::icase),

        // environment injection
        std::regex(R"(LD_PRELOAD=)", std::regex::icase),
        std::regex(R"(LD_LIBRARY_PATH=)", std::regex::icase),
        std::regex(R"(DYLD_INSERT_LIBRARIES=)", std::regex::icase),

        // dangerous redirects to system paths
        std::regex(R"(>\s*/etc/)", std::regex::icase),
        std::regex(R"(>\s*/proc/)", std::regex::icase),
        std::regex(R"(>\s*/sys/)", std::regex::icase),
        std::regex(R"(>\s*/dev/)", std::regex::icase),
    };

    for (const auto &pattern : patterns)
    {
        if (std::regex_search(command, pattern))
        {
            return "Command blocked: contains a dangerous pattern";
        }
    }

    return "";
}

// execute shell command
ToolResult ExecTool::execute(const nlohmann::json &params, const ToolContext &context)
{
    auto command = params.at("command").get<std::string>();

    // safety validation
    auto violation = validateCommand(command);

    if (!violation.empty())
    {
        return "Error: " + violation;
    }

    // determine working directory
    std::string workDir = context.workspacePath;

    if (params.contains("working_dir") && params["working_dir"].is_string())
    {
        auto dir = params["working_dir"].get<std::string>();
        bool restrict = !context.config || context.config->tools.restrictToWorkspace;
        workDir = ToolHelper::validateAndResolvePath(context.projectPath, context.workspacePath, dir, context.publicPath, restrict);
    }

    // build shell command with working directory prefix
    std::string fullCommand;

    if (!workDir.empty())
    {
        fullCommand = "cd " + ToolHelper::shellEscape(workDir) + " && ";
    }

    fullCommand += command + " 2>&1";

    auto timeoutSec = (context.config) ? context.config->tools.execTimeout : 60;

    // execute via fork/exec (POSIX) or CreateProcess (Windows) with native timeout
    auto result = ionclaw::util::ProcessRunner::run(fullCommand, timeoutSec, MAX_OUTPUT_BYTES);

    if (result.timedOut)
    {
        if (!result.output.empty())
        {
            result.output += "\n";
        }

        result.output += "[timed out after " + std::to_string(timeoutSec) + "s]";
    }
    else if (result.exitCode != 0)
    {
        if (result.exitCode < 0)
        {
            result.output += "\n[killed by signal: " + std::to_string(-result.exitCode) + "]";
        }
        else
        {
            result.output += "\n[exit code: " + std::to_string(result.exitCode) + "]";
        }
    }

    return result.output;
}

// schema definition
ToolSchema ExecTool::schema() const
{
    return {
        "exec",
        "Execute a shell command in the workspace directory with a 60-second timeout.",
        {{"type", "object"},
         {"properties",
          {{"command", {{"type", "string"}, {"description", "The shell command to execute"}}},
           {"working_dir", {{"type", "string"}, {"description", "Working directory for command execution (relative to workspace)"}}}}},
         {"required", nlohmann::json::array({"command"})}}};
}

// supported platforms
std::set<std::string> ExecTool::supportedPlatforms() const
{
    return {"linux", "macos", "windows"};
}

} // namespace builtin
} // namespace tool
} // namespace ionclaw
