#include "ionclaw/tool/ToolRegistry.hpp"

#include <algorithm>

#include "spdlog/spdlog.h"

#include "ionclaw/tool/builtin/BrowserTool.hpp"
#include "ionclaw/tool/builtin/CronTool.hpp"
#include "ionclaw/tool/builtin/EditFileTool.hpp"
#include "ionclaw/tool/builtin/ExecTool.hpp"
#include "ionclaw/tool/builtin/GenerateImageTool.hpp"
#include "ionclaw/tool/builtin/HttpClientTool.hpp"
#include "ionclaw/tool/builtin/ImageOpsTool.hpp"
#include "ionclaw/tool/builtin/InvokePlatformTool.hpp"
#include "ionclaw/tool/builtin/ListDirTool.hpp"
#include "ionclaw/tool/builtin/McpClientTool.hpp"
#include "ionclaw/tool/builtin/MemoryReadTool.hpp"
#include "ionclaw/tool/builtin/MemorySaveTool.hpp"
#include "ionclaw/tool/builtin/MemorySearchTool.hpp"
#include "ionclaw/tool/builtin/MessageTool.hpp"
#include "ionclaw/tool/builtin/ReadFileTool.hpp"
#include "ionclaw/tool/builtin/RssReaderTool.hpp"
#include "ionclaw/tool/builtin/SpawnTool.hpp"
#include "ionclaw/tool/builtin/SubagentsTool.hpp"
#include "ionclaw/tool/builtin/ToolHelper.hpp"
#include "ionclaw/tool/builtin/VisionTool.hpp"
#include "ionclaw/tool/builtin/WebFetchTool.hpp"
#include "ionclaw/tool/builtin/WebSearchTool.hpp"
#include "ionclaw/tool/builtin/WriteFileTool.hpp"

namespace ionclaw
{
namespace tool
{

ToolRegistry::ToolRegistry(bool registerDefaults)
{
    if (registerDefaults)
    {
        registerBuiltinTools();
    }
}

void ToolRegistry::registerTool(std::shared_ptr<Tool> tool)
{
    auto platforms = tool->supportedPlatforms();

    if (!platforms.empty() && platforms.find(Platform::current()) == platforms.end())
    {
        spdlog::debug("[ToolRegistry] Skipping tool '{}' (not supported on {})", tool->schema().name, Platform::current());
        return;
    }

    std::lock_guard<std::mutex> lock(mutex);
    auto name = tool->schema().name;
    tools[name] = std::move(tool);
    spdlog::debug("[ToolRegistry] Registered tool: {}", name);
}

void ToolRegistry::registerBuiltinTools()
{
    // filesystem tools (all platforms)
    registerTool(std::make_shared<builtin::ReadFileTool>());
    registerTool(std::make_shared<builtin::WriteFileTool>());
    registerTool(std::make_shared<builtin::EditFileTool>());
    registerTool(std::make_shared<builtin::ListDirTool>());

    // communication
    registerTool(std::make_shared<builtin::MessageTool>());

    // shell execution (desktop only)
    registerTool(std::make_shared<builtin::ExecTool>());

    // web tools (all platforms)
    registerTool(std::make_shared<builtin::WebSearchTool>());
    registerTool(std::make_shared<builtin::WebFetchTool>());
    registerTool(std::make_shared<builtin::HttpClientTool>());
    registerTool(std::make_shared<builtin::RssReaderTool>());
    registerTool(std::make_shared<builtin::McpClientTool>());

    // image tools (all platforms)
    registerTool(std::make_shared<builtin::GenerateImageTool>());
    registerTool(std::make_shared<builtin::ImageOpsTool>());
    registerTool(std::make_shared<builtin::VisionTool>());

    // memory tools (all platforms)
    registerTool(std::make_shared<builtin::MemorySaveTool>());
    registerTool(std::make_shared<builtin::MemoryReadTool>());
    registerTool(std::make_shared<builtin::MemorySearchTool>());

    // platform invocation (all platforms)
    registerTool(std::make_shared<builtin::InvokePlatformTool>());

    // desktop-only tools
    registerTool(std::make_shared<builtin::SpawnTool>());
    registerTool(std::make_shared<builtin::SubagentsTool>());
    registerTool(std::make_shared<builtin::CronTool>());
    registerTool(std::make_shared<builtin::BrowserTool>());

    spdlog::info("[ToolRegistry] Registered {} built-in tools", tools.size());
}

ToolResult ToolRegistry::executeTool(const std::string &name, const nlohmann::json &params, const ToolContext &context)
{
    static const std::string HINT = "\n\n[Analyze the error above and try a different approach.]";

    std::shared_ptr<Tool> tool;

    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = tools.find(name);

        // case-insensitive fallback: LLMs sometimes emit tool names with wrong casing
        if (it == tools.end())
        {
            auto lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c)
                           { return c < 0x80 ? static_cast<unsigned char>(std::tolower(c)) : c; });

            for (auto &[n, t] : tools)
            {
                auto lowerN = n;
                std::transform(lowerN.begin(), lowerN.end(), lowerN.begin(),
                               [](unsigned char c)
                               { return c < 0x80 ? static_cast<unsigned char>(std::tolower(c)) : c; });

                if (lowerN == lower)
                {
                    it = tools.find(n);
                    spdlog::debug("[ToolRegistry] Resolved '{}' → '{}' via case-insensitive match", name, n);
                    break;
                }
            }
        }

        if (it == tools.end())
        {
            spdlog::warn("[ToolRegistry] Tool not found: {}", name);

            std::string available;

            for (const auto &[n, t] : tools)
            {
                if (!available.empty())
                {
                    available += ", ";
                }

                available += n;
            }

            return "Error: Tool '" + name + "' not found. Available: " + available + HINT;
        }

        tool = it->second;
    }

    // validate parameters against schema
    auto schema = tool->schema().parameters;
    auto validationError = builtin::ToolHelper::validateParams(params, schema);

    if (!validationError.empty())
    {
        spdlog::warn("[ToolRegistry] Validation failed for tool {}: {}", name, validationError);
        return "Error: Invalid parameters for tool '" + name + "': " + validationError + HINT;
    }

    spdlog::debug("[ToolRegistry] Executing tool: {} with params: {}", name, params.dump());

    try
    {
        auto result = tool->execute(params, context);
        spdlog::debug("[ToolRegistry] Tool {} completed, output size: {} bytes", name, result.text.size());

        // append hint to error results
        if (result.text.rfind("Error", 0) == 0)
        {
            result.text += HINT;
            return result;
        }

        result.text = builtin::ToolHelper::truncateOutput(result.text, contextWindowTokens);
        return result;
    }
    catch (const std::exception &e)
    {
        spdlog::error("[ToolRegistry] Tool {} failed: {}", name, e.what());
        return "Error executing " + name + ": " + std::string(e.what()) + HINT;
    }
}

bool ToolRegistry::hasTool(const std::string &name) const
{
    std::lock_guard<std::mutex> lock(mutex);
    return tools.find(name) != tools.end();
}

std::vector<ToolSchema> ToolRegistry::getSchemas() const
{
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<ToolSchema> schemas;
    schemas.reserve(tools.size());

    for (const auto &[name, tool] : tools)
    {
        schemas.push_back(tool->schema());
    }

    return schemas;
}

std::vector<std::string> ToolRegistry::getToolNames() const
{
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<std::string> names;
    names.reserve(tools.size());

    for (const auto &[name, tool] : tools)
    {
        names.push_back(name);
    }

    return names;
}

std::vector<std::string> ToolRegistry::getToolNames(const std::vector<std::string> &allowed) const
{
    if (allowed.empty())
    {
        return getToolNames();
    }

    std::lock_guard<std::mutex> lock(mutex);
    std::vector<std::string> names;

    for (const auto &name : allowed)
    {
        if (tools.find(name) != tools.end())
        {
            names.push_back(name);
        }
    }

    return names;
}

std::vector<nlohmann::json> ToolRegistry::getOpenAiDefinitions() const
{
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<nlohmann::json> definitions;
    definitions.reserve(tools.size());

    for (const auto &[name, tool] : tools)
    {
        definitions.push_back(tool->schema().toOpenAiFormat());
    }

    return definitions;
}

std::vector<nlohmann::json> ToolRegistry::getOpenAiDefinitions(const std::vector<std::string> &allowed) const
{
    if (allowed.empty())
    {
        return getOpenAiDefinitions();
    }

    std::lock_guard<std::mutex> lock(mutex);
    std::vector<nlohmann::json> definitions;

    for (const auto &name : allowed)
    {
        auto it = tools.find(name);

        if (it != tools.end())
        {
            definitions.push_back(it->second->schema().toOpenAiFormat());
        }
    }

    return definitions;
}

std::vector<nlohmann::json> ToolRegistry::getFlatDefinitions() const
{
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<nlohmann::json> definitions;
    definitions.reserve(tools.size());

    for (const auto &[name, tool] : tools)
    {
        auto s = tool->schema();
        definitions.push_back({
            {"name", s.name},
            {"description", s.description},
            {"parameters", s.parameters},
        });
    }

    return definitions;
}

std::vector<std::string> ToolRegistry::applyToolPolicy(
    const std::vector<std::string> &toolNames,
    const ionclaw::config::ToolPolicy &policy)
{
    // build deny set (case-insensitive)
    std::set<std::string> denySet;
    for (const auto &name : policy.deny)
    {
        auto lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c)
                       { return c < 0x80 ? static_cast<unsigned char>(std::tolower(c)) : c; });
        denySet.insert(lower);
    }

    // build allow set (case-insensitive); empty = all allowed
    std::set<std::string> allowSet;
    for (const auto &name : policy.allow)
    {
        auto lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c)
                       { return c < 0x80 ? static_cast<unsigned char>(std::tolower(c)) : c; });
        allowSet.insert(lower);
    }

    std::vector<std::string> result;
    result.reserve(toolNames.size());

    for (const auto &name : toolNames)
    {
        auto lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c)
                       { return c < 0x80 ? static_cast<unsigned char>(std::tolower(c)) : c; });

        // deny takes precedence
        if (denySet.count(lower) > 0)
        {
            continue;
        }

        // if allow list is non-empty, only include listed tools
        if (!allowSet.empty() && allowSet.count(lower) == 0)
        {
            continue;
        }

        result.push_back(name);
    }

    return result;
}

} // namespace tool
} // namespace ionclaw
