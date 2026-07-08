#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <istream>
#include <mutex>
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
    static bool hasWebResources();
    static bool hasSkillResources();
    static bool hasTemplateResources();

    static void loadWebResources();
    static std::pair<const char *, size_t> getWebFile(const std::string &path);
    static bool extractTemplate(const std::string &targetDir);

    static void loadSkills();
    static std::vector<std::string> listSkills();
    static std::string getSkillContent(const std::string &name);

    // returns the embedded litellm-equivalent model capability table as raw json
    static std::string getModelCapabilitiesJson();

private:
    static std::unordered_map<std::string, std::string> webFiles;
    static std::once_flag webLoadFlag;
    static std::atomic<bool> webLoaded;

    static std::unordered_map<std::string, std::string> skillFiles;
    static std::once_flag skillsLoadFlag;
    static std::atomic<bool> skillsLoaded;

    static std::string modelsJson;
    static std::once_flag modelsLoadFlag;

    static void loadWebResourcesImpl();
    static void loadSkillsImpl();
    static void loadModelsImpl();

    static std::string normalizeZipEntry(const std::string &raw);
    static void forEachZipFile(const unsigned char *data, size_t size, const std::function<void(const std::string &name, std::istream &content)> &handler);
};

} // namespace util
} // namespace ionclaw
