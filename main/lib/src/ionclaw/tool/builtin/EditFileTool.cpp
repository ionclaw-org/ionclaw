#include "ionclaw/tool/builtin/EditFileTool.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "ionclaw/tool/builtin/ToolHelper.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

// find the closest matching section when old_text is not found
static std::string findClosestMatch(const std::string &content, const std::string &query)
{
    if (query.empty() || content.empty())
    {
        return "";
    }

    // try progressively shorter substrings of the query
    size_t bestPos = std::string::npos;
    size_t bestLen = 0;

    // try each line of the query as a search anchor
    std::istringstream queryStream(query);
    std::string queryLine;

    while (std::getline(queryStream, queryLine))
    {
        // trim whitespace
        auto start = queryLine.find_first_not_of(" \t\r\n");

        if (start == std::string::npos || queryLine.size() - start < 5)
        {
            continue;
        }

        auto trimmed = queryLine.substr(start);

        if (trimmed.size() < 5)
        {
            continue;
        }

        auto pos = content.find(trimmed);

        if (pos != std::string::npos && trimmed.size() > bestLen)
        {
            bestPos = pos;
            bestLen = trimmed.size();
        }
    }

    if (bestPos == std::string::npos)
    {
        return "";
    }

    // extract context around the match
    auto contextStart = (bestPos > 100) ? bestPos - 100 : 0;
    auto contextEnd = std::min(bestPos + bestLen + 100, content.size());
    auto context = content.substr(contextStart, contextEnd - contextStart);

    return context;
}

std::string EditFileTool::execute(const nlohmann::json &params, const ToolContext &context)
{
    auto rawPath = params.at("path").get<std::string>();
    auto oldText = params.at("old_text").get<std::string>();
    auto newText = params.at("new_text").get<std::string>();
    auto resolvedPath = ToolHelper::validateAndResolvePath(context.workspacePath, rawPath, context.publicPath);

    if (!std::filesystem::exists(resolvedPath) || !std::filesystem::is_regular_file(resolvedPath))
    {
        return "Error: file not found: " + rawPath;
    }

    std::ifstream inFile(resolvedPath, std::ios::binary);

    if (!inFile.is_open())
    {
        return "Error: cannot open file: " + rawPath;
    }

    std::ostringstream buffer;
    buffer << inFile.rdbuf();
    inFile.close();

    auto content = buffer.str();
    auto pos = content.find(oldText);

    if (pos == std::string::npos)
    {
        // try to find closest match for better error message
        auto closest = findClosestMatch(content, oldText);

        if (!closest.empty())
        {
            return "Error: old_text not found in file: " + rawPath +
                   "\n\nClosest matching section:\n---\n" + closest + "\n---\n\n" +
                   "Make sure old_text matches the file content exactly, including whitespace and indentation.";
        }

        return "Error: old_text not found in file: " + rawPath;
    }

    // check for ambiguity: require uniqueness
    auto secondPos = content.find(oldText, pos + 1);

    if (secondPos != std::string::npos)
    {
        return "Error: old_text matches multiple locations in file: " + rawPath +
               ". Provide more surrounding context to make the match unique.";
    }

    content.replace(pos, oldText.length(), newText);

    std::ofstream outFile(resolvedPath, std::ios::binary | std::ios::trunc);

    if (!outFile.is_open())
    {
        return "Error: cannot write to file: " + rawPath;
    }

    outFile << content;
    outFile.flush();

    if (outFile.fail())
    {
        return "Error: write failed (disk full or I/O error): " + rawPath;
    }

    outFile.close();

    return "File edited successfully: " + rawPath;
}

ToolSchema EditFileTool::schema() const
{
    return {
        "edit_file",
        "Find and replace exact text in a file. The old_text must match exactly and be unique in the file.",
        {{"type", "object"},
         {"properties",
          {{"path", {{"type", "string"}, {"description", "The file path to edit (absolute or relative to workspace)"}}},
           {"old_text", {{"type", "string"}, {"description", "The exact text to find and replace (must be unique in the file)"}}},
           {"new_text", {{"type", "string"}, {"description", "The replacement text"}}}}},
         {"required", nlohmann::json::array({"path", "old_text", "new_text"})}}};
}

} // namespace builtin
} // namespace tool
} // namespace ionclaw
