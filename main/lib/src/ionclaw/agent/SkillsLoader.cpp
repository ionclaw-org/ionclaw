#include "ionclaw/agent/SkillsLoader.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

#include "ionclaw/tool/Platform.hpp"
#include "ionclaw/tool/builtin/ToolHelper.hpp"
#include "ionclaw/util/EmbeddedResources.hpp"
#include "ionclaw/util/StringHelper.hpp"
#include "spdlog/spdlog.h"
#include "yaml-cpp/yaml.h"

namespace fs = std::filesystem;

namespace ionclaw
{
namespace agent
{

// skill discovery constants
const std::string SkillsLoader::SKILL_FILENAME = "SKILL.md";

SkillsLoader::SkillsLoader(const std::string &projectPath, const std::string &workspacePath)
    : projectRoot(projectPath)
    , projectSkillsDir(projectPath + "/skills")
    , workspaceSkillsDir(workspacePath.empty() ? "" : workspacePath + "/skills")
{
    if (projectPath.empty())
    {
        throw std::invalid_argument("SkillsLoader: projectPath is required");
    }
}

// scan a directory for skill folders containing SKILL.md
void SkillsLoader::scanSkillsDir(const std::string &base, std::map<std::string, std::string> &skills) const
{
    if (base.empty() || !fs::is_directory(base))
    {
        return;
    }

    try
    {
        // collect top-level directory entries
        std::vector<fs::directory_entry> entries;

        for (const auto &entry : fs::directory_iterator(base))
        {
            if (entry.is_directory())
            {
                entries.push_back(entry);
            }
        }

        std::sort(entries.begin(), entries.end());

        // check each entry for a direct or nested skill file
        for (const auto &entry : entries)
        {
            auto skillFile = entry.path() / SKILL_FILENAME;

            if (fs::exists(skillFile))
            {
                skills[entry.path().filename().string()] = skillFile.string();
                continue;
            }

            // nested: skills/source/skill-name/SKILL.md
            try
            {
                std::vector<fs::directory_entry> nested;

                for (const auto &nestedEntry : fs::directory_iterator(entry.path()))
                {
                    if (nestedEntry.is_directory())
                    {
                        nested.push_back(nestedEntry);
                    }
                }

                std::sort(nested.begin(), nested.end());

                for (const auto &nestedEntry : nested)
                {
                    auto nestedFile = nestedEntry.path() / SKILL_FILENAME;

                    if (fs::exists(nestedFile))
                    {
                        auto key = entry.path().filename().string() + "/" + nestedEntry.path().filename().string();
                        skills[key] = nestedFile.string();
                    }
                }
            }
            catch (const fs::filesystem_error &e)
            {
                spdlog::warn("[SkillsLoader] Failed to scan nested dir {}: {}", entry.path().string(), e.what());
            }
        }
    }
    catch (const fs::filesystem_error &e)
    {
        spdlog::warn("[SkillsLoader] Failed to scan skills dir {}: {}", base, e.what());
    }
}

std::map<std::string, std::string> SkillsLoader::discoverSkills() const
{
    std::map<std::string, std::string> skills;

    // tier 0: embedded built-in skills (lowest priority)
    // use special prefix "embedded:" to distinguish from filesystem paths
    for (const auto &name : util::EmbeddedResources::listSkills())
    {
        skills[name] = "embedded:" + name;
    }

    // tier 1: project skills (overrides embedded)
    scanSkillsDir(projectSkillsDir, skills);

    // tier 2: workspace skills (highest priority, overrides project and embedded)
    scanSkillsDir(workspaceSkillsDir, skills);

    return skills;
}

// parse yaml frontmatter from skill markdown content
std::pair<nlohmann::json, std::string> SkillsLoader::parseFrontmatter(const std::string &content)
{
    // thread-safe regex for concurrent skill parsing
    thread_local static const std::regex frontmatterRe(R"(^---\s*\n([\s\S]*?)\n---\s*\n)");
    std::smatch match;

    if (!std::regex_search(content, match, frontmatterRe))
    {
        return {nlohmann::json::object(), content};
    }

    nlohmann::json metadata;

    try
    {
        // extract and parse yaml block
        auto yamlStr = match[1].str();
        auto yamlNode = YAML::Load(yamlStr);

        if (yamlNode.IsMap())
        {
            for (const auto &pair : yamlNode)
            {
                auto key = pair.first.as<std::string>();

                if (pair.second.IsSequence())
                {
                    nlohmann::json arr = nlohmann::json::array();

                    for (const auto &item : pair.second)
                    {
                        if (item.IsScalar())
                        {
                            arr.push_back(item.as<std::string>());
                        }
                    }

                    metadata[key] = arr;
                }
                else if (pair.second.IsScalar())
                {
                    try
                    {
                        metadata[key] = pair.second.as<bool>();
                    }
                    catch (const YAML::BadConversion &)
                    {
                        metadata[key] = pair.second.as<std::string>();
                    }
                }
            }
        }
    }
    catch (const std::exception &e)
    {
        spdlog::debug("[SkillsLoader] Failed to parse frontmatter: {}", e.what());
    }

    // extract body after frontmatter
    auto body = content.substr(match[0].length());
    return {metadata, body};
}

// determine skill source tier from its path
std::string SkillsLoader::resolveSource(const std::string &path) const
{
    if (path.find("embedded:") == 0)
    {
        return "builtin";
    }

    if (!projectSkillsDir.empty() && path.find(projectSkillsDir) == 0)
    {
        return "project";
    }

    return "workspace";
}

// check if skill platform requirement matches current platform
bool SkillsLoader::matchesPlatform(const nlohmann::json &metadata)
{
    if (!metadata.contains("platform"))
    {
        return true;
    }

    auto currentStr = tool::Platform::current();

    auto currentLower = ionclaw::util::StringHelper::toLower(currentStr);

    // platform can be a string or an array of strings
    const auto &val = metadata["platform"];

    if (val.is_string())
    {
        return ionclaw::util::StringHelper::toLower(val.get<std::string>()) == currentLower;
    }

    if (val.is_array())
    {
        for (const auto &item : val)
        {
            if (item.is_string() && ionclaw::util::StringHelper::toLower(item.get<std::string>()) == currentLower)
            {
                return true;
            }
        }

        return false;
    }

    return true;
}

// read skill content from embedded resources or filesystem
std::string SkillsLoader::readSkillContent(const std::string &path)
{
    if (path.find("embedded:") == 0)
    {
        auto name = path.substr(9);
        return util::EmbeddedResources::getSkillContent(name);
    }

    std::ifstream file(path, std::ios::binary);

    if (!file.is_open())
    {
        return "";
    }

    std::ostringstream buf;
    buf << file.rdbuf();
    return buf.str();
}

// list all available skills with metadata
std::vector<SkillInfo> SkillsLoader::listSkills() const
{
    std::vector<SkillInfo> result;

    for (const auto &[key, path] : discoverSkills())
    {
        try
        {
            auto content = readSkillContent(path);

            if (content.empty())
            {
                continue;
            }

            // parse metadata and filter by platform
            auto [metadata, body] = parseFrontmatter(content);

            if (!matchesPlatform(metadata))
            {
                continue;
            }

            // build skill info from metadata
            SkillInfo info;
            info.name = key;
            info.description = metadata.value("description", "");
            info.always = metadata.value("always", false);
            info.available = true;
            info.source = resolveSource(path);
            info.location = ionclaw::tool::builtin::ToolHelper::toRelativePath(path, projectRoot);

            // extract publisher from key prefix
            auto slashPos = key.find('/');

            if (slashPos != std::string::npos)
            {
                info.publisher = key.substr(0, slashPos);
            }

            result.push_back(info);
        }
        catch (const std::exception &e)
        {
            spdlog::warn("[SkillsLoader] Failed to load skill {}: {}", key, e.what());
        }
    }

    // sort by short name (after last slash)
    std::sort(result.begin(), result.end(), [](const SkillInfo &a, const SkillInfo &b)
              {
        auto aName = a.name;
        auto bName = b.name;
        auto aSlash = aName.rfind('/');
        auto bSlash = bName.rfind('/');
        auto aShort = (aSlash != std::string::npos) ? aName.substr(aSlash + 1) : aName;
        auto bShort = (bSlash != std::string::npos) ? bName.substr(bSlash + 1) : bName;
        return aShort < bShort; });

    return result;
}

// load skill body content (without frontmatter), trimmed
std::string SkillsLoader::loadSkill(const std::string &name) const
{
    auto skills = discoverSkills();
    auto it = skills.find(name);

    if (it == skills.end())
    {
        return "";
    }

    try
    {
        auto content = readSkillContent(it->second);

        if (content.empty())
        {
            return "";
        }

        auto [metadata, body] = parseFrontmatter(content);

        // trim
        auto start = body.find_first_not_of(" \t\n\r");
        auto end = body.find_last_not_of(" \t\n\r");

        if (start == std::string::npos)
        {
            return "";
        }

        return body.substr(start, end - start + 1);
    }
    catch (const std::exception &e)
    {
        spdlog::warn("[SkillsLoader] Failed to load skill {}: {}", name, e.what());
        return "";
    }
}

// load raw skill content including frontmatter
std::string SkillsLoader::loadSkillRaw(const std::string &name) const
{
    auto skills = discoverSkills();
    auto it = skills.find(name);

    if (it == skills.end())
    {
        return "";
    }

    try
    {
        return readSkillContent(it->second);
    }
    catch (const std::exception &e)
    {
        spdlog::warn("[SkillsLoader] Failed to load skill raw {}: {}", name, e.what());
        return "";
    }
}

// save skill content to disk (rejects embedded skills)
std::string SkillsLoader::saveSkill(const std::string &name, const std::string &content)
{
    auto skills = discoverSkills();
    auto it = skills.find(name);

    if (it == skills.end())
    {
        return "Skill not found";
    }

    if (it->second.find("embedded:") == 0)
    {
        return "Cannot modify built-in skill";
    }

    try
    {
        std::ofstream file(it->second, std::ios::binary);

        if (!file.is_open())
        {
            return "Failed to open file for writing";
        }

        file << content;
        return "";
    }
    catch (const std::exception &e)
    {
        return e.what();
    }
}

// delete skill directory from disk (rejects embedded skills)
std::string SkillsLoader::deleteSkill(const std::string &name)
{
    auto skills = discoverSkills();
    auto it = skills.find(name);

    if (it == skills.end())
    {
        return "Skill not found";
    }

    if (it->second.find("embedded:") == 0)
    {
        return "Cannot delete built-in skill";
    }

    try
    {
        auto skillDir = fs::path(it->second).parent_path();

        // prevent deleting the base skills directory itself
        auto canonical = fs::weakly_canonical(skillDir).string();
        auto projCanonical = projectSkillsDir.empty() ? "" : fs::weakly_canonical(projectSkillsDir).string();
        auto wsCanonical = workspaceSkillsDir.empty() ? "" : fs::weakly_canonical(workspaceSkillsDir).string();

        if ((!projCanonical.empty() && canonical == projCanonical) ||
            (!wsCanonical.empty() && canonical == wsCanonical))
        {
            return "Cannot delete skills base directory";
        }

        fs::remove_all(skillDir);
        return "";
    }
    catch (const std::exception &e)
    {
        return e.what();
    }
}

// collect skills marked always=true for inline inclusion in prompts
std::vector<std::pair<std::string, std::string>> SkillsLoader::getAlwaysSkills() const
{
    std::vector<std::pair<std::string, std::string>> result;

    for (const auto &[key, path] : discoverSkills())
    {
        try
        {
            auto content = readSkillContent(path);

            if (content.empty())
            {
                continue;
            }

            // filter by always flag and platform
            auto [metadata, body] = parseFrontmatter(content);

            if (!metadata.value("always", false))
            {
                continue;
            }

            if (!matchesPlatform(metadata))
            {
                continue;
            }

            auto skillName = metadata.value("name", key);

            // trim body
            auto start = body.find_first_not_of(" \t\n\r");
            auto end = body.find_last_not_of(" \t\n\r");

            if (start != std::string::npos)
            {
                result.emplace_back(skillName, body.substr(start, end - start + 1));
            }
        }
        catch (const std::exception &e)
        {
            spdlog::warn("[SkillsLoader] Failed to load always-skill {}: {}", key, e.what());
        }
    }

    return result;
}

// build xml summary of all skills for system prompt
std::string SkillsLoader::buildSkillsSummary() const
{
    auto skills = listSkills();

    if (skills.empty())
    {
        return "";
    }

    std::ostringstream out;
    out << "<available_skills>\n";

    for (const auto &s : skills)
    {
        auto status = s.available ? "available" : "unavailable";
        auto alwaysTag = s.always ? " always=\"true\"" : "";
        out << "  <skill" << alwaysTag << ">"
            << "<name>" << s.name << "</name>"
            << "<description>" << s.description << "</description>"
            << "<location>" << s.location << "</location>"
            << "<status>" << status << "</status>"
            << "</skill>\n";
    }

    out << "</available_skills>";
    return out.str();
}

} // namespace agent
} // namespace ionclaw
