#include "ionclaw/util/EmbeddedResources.hpp"

#include <filesystem>
#include <fstream>
#include <functional>
#include <istream>
#include <sstream>

#include "spdlog/spdlog.h"

#ifdef IONCLAW_EMBEDDED_RESOURCES
#include "embedded_models.hpp"
#include "embedded_skills.hpp"
#include "embedded_template.hpp"
#include "embedded_web.hpp"

#include "Poco/StreamCopier.h"
#include "Poco/Zip/ZipArchive.h"
#include "Poco/Zip/ZipStream.h"
#endif

namespace fs = std::filesystem;

namespace ionclaw
{
namespace util
{

std::unordered_map<std::string, std::string> EmbeddedResources::webFiles;
std::once_flag EmbeddedResources::webLoadFlag;
std::atomic<bool> EmbeddedResources::webLoaded{false};

std::unordered_map<std::string, std::string> EmbeddedResources::skillFiles;
std::once_flag EmbeddedResources::skillsLoadFlag;
std::atomic<bool> EmbeddedResources::skillsLoaded{false};

std::string EmbeddedResources::modelsJson;
std::once_flag EmbeddedResources::modelsLoadFlag;

#ifdef IONCLAW_EMBEDDED_RESOURCES

std::string EmbeddedResources::normalizeZipEntry(const std::string &raw)
{
    // directory entries carry no content
    if (raw.empty() || raw.back() == '/')
    {
        return "";
    }

    // strip the leading "./" that the cmake tar step prepends
    if (raw.size() > 2 && raw[0] == '.' && raw[1] == '/')
    {
        return raw.substr(2);
    }

    return raw;
}

void EmbeddedResources::forEachZipFile(const unsigned char *data, size_t size, const std::function<void(const std::string &name, std::istream &content)> &handler)
{
    std::string zipData(reinterpret_cast<const char *>(data), size);
    std::istringstream zipStream(zipData);

    Poco::Zip::ZipArchive archive(zipStream);

    for (auto it = archive.headerBegin(); it != archive.headerEnd(); ++it)
    {
        auto name = normalizeZipEntry(it->first);

        if (name.empty())
        {
            continue;
        }

        // each entry is read from the start of the shared stream using its own header
        zipStream.clear();
        zipStream.seekg(0);

        Poco::Zip::ZipInputStream zis(zipStream, it->second);
        handler(name, zis);
    }
}

#endif

bool EmbeddedResources::hasWebResources()
{
#ifdef IONCLAW_EMBEDDED_RESOURCES
    return ionclaw_embedded_web::getSize() > 0;
#else
    return false;
#endif
}

bool EmbeddedResources::hasSkillResources()
{
#ifdef IONCLAW_EMBEDDED_RESOURCES
    return ionclaw_embedded_skills::getSize() > 0;
#else
    return false;
#endif
}

bool EmbeddedResources::hasTemplateResources()
{
#ifdef IONCLAW_EMBEDDED_RESOURCES
    return ionclaw_embedded_template::getSize() > 0;
#else
    return false;
#endif
}

void EmbeddedResources::loadWebResources()
{
    std::call_once(webLoadFlag, loadWebResourcesImpl);
}

void EmbeddedResources::loadWebResourcesImpl()
{
#ifdef IONCLAW_EMBEDDED_RESOURCES
    try
    {
        // clang-format off
        forEachZipFile(ionclaw_embedded_web::getData(), ionclaw_embedded_web::getSize(), [](const std::string &name, std::istream &content) {
            std::ostringstream out;
            Poco::StreamCopier::copyStream(content, out);
            webFiles[name] = out.str();
        });
        // clang-format on

        webLoaded.store(true, std::memory_order_release);
        spdlog::info("[EmbeddedResources] Loaded {} web files from embedded resources", webFiles.size());
    }
    catch (const std::exception &e)
    {
        spdlog::error("[EmbeddedResources] Failed to load web resources: {}", e.what());
    }
#endif
}

std::pair<const char *, size_t> EmbeddedResources::getWebFile(const std::string &path)
{
    if (!webLoaded.load(std::memory_order_acquire))
    {
        return {nullptr, 0};
    }

    auto it = webFiles.find(path);

    if (it != webFiles.end())
    {
        return {it->second.data(), it->second.size()};
    }

    return {nullptr, 0};
}

bool EmbeddedResources::extractTemplate(const std::string &targetDir)
{
#ifdef IONCLAW_EMBEDDED_RESOURCES
    try
    {
        int extracted = 0;

        // clang-format off
        forEachZipFile(ionclaw_embedded_template::getData(), ionclaw_embedded_template::getSize(), [&](const std::string &name, std::istream &content) {
            auto outputPath = fs::path(targetDir) / name;

            // never overwrite a file the user may have edited
            if (fs::exists(outputPath))
            {
                spdlog::debug("[EmbeddedResources] Skipping existing: {}", name);
                return;
            }

            std::error_code ec;
            fs::create_directories(outputPath.parent_path(), ec);

            std::ofstream outFile(outputPath.string(), std::ios::binary);

            if (!outFile.is_open())
            {
                return;
            }

            Poco::StreamCopier::copyStream(content, outFile);
            extracted++;
            spdlog::debug("[EmbeddedResources] Extracted: {}", name);
        });
        // clang-format on

        spdlog::info("[EmbeddedResources] Extracted {} template files to {}", extracted, targetDir);
        return true;
    }
    catch (const std::exception &e)
    {
        spdlog::error("[EmbeddedResources] Template extraction failed: {}", e.what());
        return false;
    }
#else
    return false;
#endif
}

void EmbeddedResources::loadSkills()
{
    std::call_once(skillsLoadFlag, loadSkillsImpl);
}

void EmbeddedResources::loadSkillsImpl()
{
#ifdef IONCLAW_EMBEDDED_RESOURCES
    try
    {
        // skills zip has flat paths: <skill-name>/SKILL.md
        // clang-format off
        forEachZipFile(ionclaw_embedded_skills::getData(), ionclaw_embedded_skills::getSize(), [](const std::string &name, std::istream &content) {
            auto slashPos = name.find('/');

            if (slashPos == std::string::npos || name.substr(slashPos + 1) != "SKILL.md")
            {
                return;
            }

            std::ostringstream out;
            Poco::StreamCopier::copyStream(content, out);
            skillFiles[name.substr(0, slashPos)] = out.str();
        });
        // clang-format on

        skillsLoaded.store(true, std::memory_order_release);
        spdlog::info("[EmbeddedResources] Loaded {} built-in skills", skillFiles.size());
    }
    catch (const std::exception &e)
    {
        spdlog::error("[EmbeddedResources] Failed to load embedded skills: {}", e.what());
    }
#endif
}

std::vector<std::string> EmbeddedResources::listSkills()
{
    if (!skillsLoaded)
    {
        loadSkills();
    }

    std::vector<std::string> names;
    names.reserve(skillFiles.size());

    for (const auto &pair : skillFiles)
    {
        names.push_back(pair.first);
    }

    return names;
}

std::string EmbeddedResources::getSkillContent(const std::string &name)
{
    if (!skillsLoaded)
    {
        loadSkills();
    }

    auto it = skillFiles.find(name);

    if (it != skillFiles.end())
    {
        return it->second;
    }

    return "";
}

void EmbeddedResources::loadModelsImpl()
{
#ifdef IONCLAW_EMBEDDED_RESOURCES
    try
    {
        // the models zip holds a single file, model_capabilities.json
        // clang-format off
        forEachZipFile(ionclaw_embedded_models::getData(), ionclaw_embedded_models::getSize(), [](const std::string &name, std::istream &content) {
            if (name == "model_capabilities.json")
            {
                std::ostringstream out;
                Poco::StreamCopier::copyStream(content, out);
                modelsJson = out.str();
            }
        });
        // clang-format on

        spdlog::info("[EmbeddedResources] Loaded model capability table ({} bytes)", modelsJson.size());
    }
    catch (const std::exception &e)
    {
        spdlog::error("[EmbeddedResources] Failed to load embedded model capabilities: {}", e.what());
    }
#endif
}

std::string EmbeddedResources::getModelCapabilitiesJson()
{
    std::call_once(modelsLoadFlag, loadModelsImpl);
    return modelsJson;
}

} // namespace util
} // namespace ionclaw
