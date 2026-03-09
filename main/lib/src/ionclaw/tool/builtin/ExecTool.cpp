#include "ionclaw/tool/builtin/ExecTool.hpp"

#include <array>

#include "ionclaw/config/Config.hpp"
#include "ionclaw/tool/builtin/ToolHelper.hpp"
#include "ionclaw/util/PipeGuard.hpp"
#include "ionclaw/util/StringHelper.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

// maximum output size before truncation
const size_t ExecTool::MAX_OUTPUT_BYTES = 200 * 1024; // 200KB

// dangerous command patterns that should be blocked
const std::vector<std::regex> ExecTool::DANGEROUS_PATTERNS = {
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

// validate command against dangerous patterns
std::string ExecTool::validateCommand(const std::string &command)
{
    // check for path traversal
    if (command.find("../") != std::string::npos || command.find("..\\") != std::string::npos)
    {
        return "Command contains path traversal sequences (../ or ..\\)";
    }

    for (const auto &pattern : DANGEROUS_PATTERNS)
    {
        if (std::regex_search(command, pattern))
        {
            return "Command blocked: contains a dangerous pattern";
        }
    }

    return "";
}

// execute shell command
std::string ExecTool::execute(const nlohmann::json &params, const ToolContext &context)
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
        workDir = ToolHelper::validateAndResolvePath(context.workspacePath, dir, context.publicPath);
    }

    // build full command with working directory and timeout
    std::string fullCommand;

    if (!workDir.empty())
    {
        fullCommand = "cd " + ToolHelper::shellEscape(workDir) + " && ";
    }

    auto timeoutSec = (context.config) ? context.config->tools.execTimeout : 60;

#ifdef _WIN32
    fullCommand += command + " 2>&1";
#else
    fullCommand += "timeout " + std::to_string(timeoutSec) + " " + command + " 2>&1";
#endif

    // execute via popen (RAII guard ensures pclose on all exit paths)
    std::array<char, 4096> buffer;
    std::string output;

    ionclaw::util::PipeGuard pipe(fullCommand.c_str());

    if (!pipe)
    {
        return "Error: failed to execute command";
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr)
    {
        output += buffer.data();

        if (output.size() > MAX_OUTPUT_BYTES)
        {
            pipe.close();
            return ionclaw::util::StringHelper::utf8SafeTruncate(output, MAX_OUTPUT_BYTES) + "\n... [output truncated at 200KB]";
        }
    }

    // collect exit code
    auto exitCode = pipe.close();

#ifdef _WIN32
    if (exitCode != 0)
    {
        output += "\n[exit code: " + std::to_string(exitCode) + "]";
    }
#else
    if (exitCode != 0)
    {
        output += "\n[exit code: " + std::to_string(WEXITSTATUS(exitCode)) + "]";
    }
#endif

    // final truncation check
    if (output.size() > MAX_OUTPUT_BYTES)
    {
        return ionclaw::util::StringHelper::utf8SafeTruncate(output, MAX_OUTPUT_BYTES) + "\n... [output truncated at 200KB]";
    }

    return output;
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
