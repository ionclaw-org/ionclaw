#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "nlohmann/json.hpp"

namespace ionclaw
{
namespace agent
{

struct SkillInfo
{
    std::string name;
    std::string description;
    bool available = true;
    bool always = false;
    std::string source; // "builtin", "project", "workspace"
    std::string publisher;
    std::string location;
    std::string agent; // agent name that owns this workspace skill (empty for builtin/project)
};

class SkillsLoader
{
public:
    SkillsLoader(const std::string &projectPath, const std::string &workspacePath);

    std::vector<SkillInfo> listSkills() const;
    std::string loadSkill(const std::string &name) const;
    std::string loadSkillRaw(const std::string &name) const;
    std::string saveSkill(const std::string &name, const std::string &content);
    std::string deleteSkill(const std::string &name);

    std::vector<std::pair<std::string, std::string>> getAlwaysSkills() const;
    std::string buildSkillsSummary() const;

    // skill discovery (name → file path)
    std::map<std::string, std::string> discoverSkills() const;

private:
    std::string projectSkillsDir;
    std::string workspaceSkillsDir;

    // skill discovery constants
    static const std::string SKILL_FILENAME;
    static std::pair<nlohmann::json, std::string> parseFrontmatter(const std::string &content);
    std::string resolveSource(const std::string &path) const;
    void scanSkillsDir(const std::string &base, std::map<std::string, std::string> &skills) const;

    // platform and content helpers
    static bool matchesPlatform(const nlohmann::json &metadata);
    static std::string readSkillContent(const std::string &path);
};

} // namespace agent
} // namespace ionclaw
