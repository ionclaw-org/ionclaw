#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "ionclaw/config/Config.hpp"
#include "ionclaw/tool/Tool.hpp"

namespace ionclaw
{
namespace tool
{

class ToolRegistry
{
public:
    explicit ToolRegistry(bool registerDefaults = true);

    void registerTool(std::shared_ptr<Tool> tool);
    void registerBuiltinTools();

    std::string executeTool(const std::string &name, const nlohmann::json &params, const ToolContext &context);
    bool hasTool(const std::string &name) const;

    std::vector<ToolSchema> getSchemas() const;
    std::vector<std::string> getToolNames() const;
    std::vector<std::string> getToolNames(const std::vector<std::string> &allowed) const;
    std::vector<nlohmann::json> getOpenAiDefinitions() const;
    std::vector<nlohmann::json> getOpenAiDefinitions(const std::vector<std::string> &allowed) const;
    std::vector<nlohmann::json> getFlatDefinitions() const;

    static std::vector<std::string> applyToolPolicy(
        const std::vector<std::string> &toolNames,
        const ionclaw::config::ToolPolicy &policy);

    void setContextWindowTokens(int tokens) { contextWindowTokens = tokens; }

private:
    std::map<std::string, std::shared_ptr<Tool>> tools;
    mutable std::mutex mutex;
    int contextWindowTokens = 0;
};

} // namespace tool
} // namespace ionclaw
