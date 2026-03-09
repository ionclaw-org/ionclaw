#include "ionclaw/tool/builtin/ToolHelper.hpp"

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <stdexcept>

#include "ionclaw/util/StringHelper.hpp"

namespace fs = std::filesystem;

namespace ionclaw
{
namespace tool
{
namespace builtin
{

std::string ToolHelper::normalizePath(const std::string &workspacePath, const std::string &rawPath)
{
    fs::path resolved;

    if (fs::path(rawPath).is_absolute())
    {
        resolved = fs::weakly_canonical(fs::path(rawPath));
    }
    else
    {
        resolved = fs::weakly_canonical(fs::path(workspacePath) / rawPath);
    }

    return resolved.string();
}

bool ToolHelper::isPathWithinWorkspace(const std::string &workspacePath, const std::string &resolvedPath)
{
    auto workspace = fs::weakly_canonical(fs::path(workspacePath)).string();
    auto target = resolvedPath;

    if (workspace.back() != '/')
    {
        workspace += '/';
    }

    return target == workspace.substr(0, workspace.size() - 1) || target.rfind(workspace, 0) == 0;
}

std::string ToolHelper::validateAndResolvePath(const std::string &workspacePath, const std::string &rawPath,
                                               const std::string &publicPath)
{
    if (workspacePath.empty())
    {
        throw std::runtime_error("Workspace path is not configured");
    }

    std::string resolved;

    if (!publicPath.empty() && rawPath.size() > 7 && rawPath.substr(0, 7) == "public/")
    {
        resolved = normalizePath(publicPath, rawPath.substr(7));
    }
    else
    {
        resolved = normalizePath(workspacePath, rawPath);
    }

    bool inWorkspace = isPathWithinWorkspace(workspacePath, resolved);
    bool inPublic = !publicPath.empty() && isPathWithinWorkspace(publicPath, resolved);

    if (!inWorkspace && !inPublic)
    {
        throw std::runtime_error("Path is outside the workspace: " + rawPath);
    }

    return resolved;
}

std::string ToolHelper::truncateOutput(const std::string &output, int contextWindowTokens)
{
    int maxChars = MAX_TOOL_RESULT_CHARS;

    if (contextWindowTokens > 0)
    {
        auto contextMax = static_cast<int>(std::min(
            static_cast<double>(contextWindowTokens) * TOOL_RESULT_CONTEXT_SHARE * 4.0,
            static_cast<double>(MAX_TOOL_RESULT_CHARS)));
        maxChars = std::min(maxChars, contextMax);
    }

    if (output.size() > static_cast<size_t>(maxChars))
    {
        // keep head (75%) + tail (25%, max 2000 chars) to preserve errors at end
        auto tailSize = std::min(maxChars / 4, 2000);
        auto headSize = maxChars - tailSize;

        auto head = ionclaw::util::StringHelper::utf8SafeTruncate(output, headSize);

        // UTF-8 safe tail extraction: skip continuation bytes at start of tail
        auto tailStart = output.size() - static_cast<size_t>(tailSize);

        while (tailStart < output.size() &&
               (static_cast<unsigned char>(output[tailStart]) & 0xC0) == 0x80)
        {
            tailStart++;
        }

        auto tail = output.substr(tailStart);

        // find clean line boundary for tail
        auto lineStart = tail.find('\n');

        if (lineStart != std::string::npos && lineStart < static_cast<size_t>(tailSize / 2))
        {
            tail = tail.substr(lineStart + 1);
        }

        auto omitted = static_cast<int64_t>(output.size()) - headSize - tailSize;

        return head +
               "\n\n[... " + std::to_string(omitted) + " chars omitted ...]\n\n" +
               tail;
    }

    return output;
}

std::string ToolHelper::shellEscape(const std::string &input)
{
    std::string result = "'";

    for (char c : input)
    {
        if (c == '\'')
        {
            result += "'\\''";
        }
        else
        {
            result += c;
        }
    }

    result += "'";
    return result;
}

std::string ToolHelper::escapeForJs(const std::string &input)
{
    std::string result;
    result.reserve(input.size() + 16);

    for (char c : input)
    {
        switch (c)
        {
        case '\\':
            result += "\\\\";
            break;
        case '\'':
            result += "\\'";
            break;
        case '"':
            result += "\\\"";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            result += c;
            break;
        }
    }

    return result;
}

std::string ToolHelper::validateParams(const nlohmann::json &params, const nlohmann::json &schema)
{
    if (!schema.is_object())
    {
        return "";
    }

    std::vector<std::string> errors;

    // check required fields
    if (schema.contains("required") && schema["required"].is_array())
    {
        for (const auto &field : schema["required"])
        {
            auto name = field.get<std::string>();

            if (!params.contains(name) || params[name].is_null())
            {
                errors.push_back("missing required parameter: " + name);
            }
        }
    }

    // validate property types
    if (schema.contains("properties") && schema["properties"].is_object())
    {
        for (auto &[key, propSchema] : schema["properties"].items())
        {
            if (!params.contains(key))
            {
                continue;
            }

            const auto &value = params[key];
            auto expectedType = propSchema.value("type", "");

            if (expectedType == "string" && !value.is_string())
            {
                errors.push_back(key + ": expected string");
            }
            else if (expectedType == "integer" && !value.is_number_integer())
            {
                errors.push_back(key + ": expected integer");
            }
            else if (expectedType == "number" && !value.is_number())
            {
                errors.push_back(key + ": expected number");
            }
            else if (expectedType == "boolean" && !value.is_boolean())
            {
                errors.push_back(key + ": expected boolean");
            }
            else if (expectedType == "array" && !value.is_array())
            {
                errors.push_back(key + ": expected array");
            }
            else if (expectedType == "object" && !value.is_object())
            {
                errors.push_back(key + ": expected object");
            }

            // check enum
            if (propSchema.contains("enum") && propSchema["enum"].is_array() && value.is_string())
            {
                auto val = value.get<std::string>();
                bool found = false;

                for (const auto &allowed : propSchema["enum"])
                {
                    if (allowed.is_string() && allowed.get<std::string>() == val)
                    {
                        found = true;
                        break;
                    }
                }

                if (!found)
                {
                    errors.push_back(key + ": invalid value '" + val + "'");
                }
            }

            // check minimum/maximum for integers
            if (value.is_number_integer())
            {
                if (propSchema.contains("minimum"))
                {
                    auto minVal = propSchema["minimum"].get<int>();

                    if (value.get<int>() < minVal)
                    {
                        errors.push_back(key + ": must be >= " + std::to_string(minVal));
                    }
                }

                if (propSchema.contains("maximum"))
                {
                    auto maxVal = propSchema["maximum"].get<int>();

                    if (value.get<int>() > maxVal)
                    {
                        errors.push_back(key + ": must be <= " + std::to_string(maxVal));
                    }
                }
            }
        }
    }

    if (errors.empty())
    {
        return "";
    }

    std::ostringstream oss;

    for (size_t i = 0; i < errors.size(); ++i)
    {
        if (i > 0)
        {
            oss << "; ";
        }

        oss << errors[i];
    }

    return oss.str();
}

} // namespace builtin
} // namespace tool
} // namespace ionclaw
