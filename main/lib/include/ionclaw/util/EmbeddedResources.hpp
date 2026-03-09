#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ionclaw
{
namespace util
{

class EmbeddedResources
{
public:
    // check if embedded resources are available (compile-time)
    static bool hasWebResources();
    static bool hasSkillResources();
    static bool hasTemplateResources();

    // decompress embedded web ZIP into memory (call once at startup)
    static void loadWebResources();

    // get a web file by path (e.g. "index.html", "assets/index-xxx.js")
    // returns {pointer, size} or {nullptr, 0} if not found
    static std::pair<const char *, size_t> getWebFile(const std::string &path);

    // extract embedded template to target directory (skips existing files)
    static bool extractTemplate(const std::string &targetDir);

    // load embedded skills from template ZIP into memory (call once at startup)
    static void loadSkills();

    // list embedded skill names (e.g. "memory", "weather")
    static std::vector<std::string> listSkills();

    // get embedded skill content by name, returns empty string if not found
    static std::string getSkillContent(const std::string &name);

private:
    static std::unordered_map<std::string, std::string> webFiles;
    static bool webLoaded;

    // skill name -> SKILL.md content
    static std::unordered_map<std::string, std::string> skillFiles;
    static bool skillsLoaded;
};

} // namespace util
} // namespace ionclaw
