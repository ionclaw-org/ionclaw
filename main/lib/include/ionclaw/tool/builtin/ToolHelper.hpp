#pragma once

#include <string>

#include "nlohmann/json.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

class ToolHelper
{
public:
    static std::string normalizePath(const std::string &workspacePath, const std::string &rawPath);
    static bool isPathWithinWorkspace(const std::string &workspacePath, const std::string &resolvedPath);
    static std::string validateAndResolvePath(const std::string &workspacePath, const std::string &rawPath,
                                              const std::string &publicPath);
    static std::string truncateOutput(const std::string &output, int contextWindowTokens = 0);
    static std::string shellEscape(const std::string &input);
    static std::string escapeForJs(const std::string &input);
    static std::string validateParams(const nlohmann::json &params, const nlohmann::json &schema);

private:
    // truncation limits
    static constexpr int MAX_TOOL_RESULT_CHARS = 400000;
    static constexpr double TOOL_RESULT_CONTEXT_SHARE = 0.3;
};

} // namespace builtin
} // namespace tool
} // namespace ionclaw
