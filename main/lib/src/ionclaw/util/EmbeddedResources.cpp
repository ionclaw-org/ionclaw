#include "ionclaw/util/EmbeddedResources.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "spdlog/spdlog.h"

#ifdef IONCLAW_EMBEDDED_RESOURCES
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
bool EmbeddedResources::webLoaded = false;

std::unordered_map<std::string, std::string> EmbeddedResources::skillFiles;
bool EmbeddedResources::skillsLoaded = false;

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
#ifdef IONCLAW_EMBEDDED_RESOURCES
    if (webLoaded)
    {
        return;
    }

    try
    {
        auto data = ionclaw_embedded_web::getData();
        auto size = ionclaw_embedded_web::getSize();

        std::string zipData(reinterpret_cast<const char *>(data), size);
        std::istringstream zipStream(zipData);

        Poco::Zip::ZipArchive archive(zipStream);

        for (auto it = archive.headerBegin(); it != archive.headerEnd(); ++it)
        {
            auto entryName = it->first;

            // skip directory entries
            if (entryName.empty() || entryName.back() == '/')
            {
                continue;
            }

            // strip leading "./" if present (cmake tar adds it)
            if (entryName.size() > 2 && entryName[0] == '.' && entryName[1] == '/')
            {
                entryName = entryName.substr(2);
            }

            zipStream.clear();
            zipStream.seekg(0);

            Poco::Zip::ZipInputStream zis(zipStream, it->second);
            std::ostringstream content;
            Poco::StreamCopier::copyStream(zis, content);

            webFiles[entryName] = content.str();
        }

        webLoaded = true;
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
    if (!webLoaded)
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
        auto data = ionclaw_embedded_template::getData();
        auto size = ionclaw_embedded_template::getSize();

        std::string zipData(reinterpret_cast<const char *>(data), size);
        std::istringstream zipStream(zipData);

        Poco::Zip::ZipArchive archive(zipStream);
        int extracted = 0;

        for (auto it = archive.headerBegin(); it != archive.headerEnd(); ++it)
        {
            auto entryName = it->first;

            // skip directory entries
            if (entryName.empty() || entryName.back() == '/')
            {
                continue;
            }

            // strip leading "./" if present
            if (entryName.size() > 2 && entryName[0] == '.' && entryName[1] == '/')
            {
                entryName = entryName.substr(2);
            }

            auto outputPath = fs::path(targetDir) / entryName;

            // skip existing files
            if (fs::exists(outputPath))
            {
                spdlog::debug("[EmbeddedResources] Skipping existing: {}", entryName);
                continue;
            }

            // create parent directories
            std::error_code ec;
            fs::create_directories(outputPath.parent_path(), ec);

            // extract
            zipStream.clear();
            zipStream.seekg(0);

            Poco::Zip::ZipInputStream zis(zipStream, it->second);

            std::ofstream outFile(outputPath.string(), std::ios::binary);

            if (outFile.is_open())
            {
                char buffer[8192];

                while (zis.good())
                {
                    zis.read(buffer, sizeof(buffer));
                    auto bytesRead = zis.gcount();

                    if (bytesRead > 0)
                    {
                        outFile.write(buffer, bytesRead);
                    }
                }

                outFile.close();
                extracted++;
                spdlog::debug("[EmbeddedResources] Extracted: {}", entryName);
            }
        }

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
#ifdef IONCLAW_EMBEDDED_RESOURCES
    if (skillsLoaded)
    {
        return;
    }

    try
    {
        auto data = ionclaw_embedded_skills::getData();
        auto size = ionclaw_embedded_skills::getSize();

        std::string zipData(reinterpret_cast<const char *>(data), size);
        std::istringstream zipStream(zipData);

        Poco::Zip::ZipArchive archive(zipStream);

        // skills ZIP has flat paths: <skill-name>/SKILL.md
        for (auto it = archive.headerBegin(); it != archive.headerEnd(); ++it)
        {
            auto entryName = it->first;

            if (entryName.empty() || entryName.back() == '/')
            {
                continue;
            }

            // strip leading "./"
            if (entryName.size() > 2 && entryName[0] == '.' && entryName[1] == '/')
            {
                entryName = entryName.substr(2);
            }

            // match <name>/SKILL.md
            auto slashPos = entryName.find('/');

            if (slashPos == std::string::npos)
            {
                continue;
            }

            auto skillName = entryName.substr(0, slashPos);
            auto filename = entryName.substr(slashPos + 1);

            if (filename != "SKILL.md")
            {
                continue;
            }

            zipStream.clear();
            zipStream.seekg(0);

            Poco::Zip::ZipInputStream zis(zipStream, it->second);
            std::ostringstream content;
            Poco::StreamCopier::copyStream(zis, content);

            skillFiles[skillName] = content.str();
        }

        skillsLoaded = true;
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

} // namespace util
} // namespace ionclaw
