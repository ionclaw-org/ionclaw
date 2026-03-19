#include "ionclaw/server/Routes.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "Poco/URI.h"
#include "Poco/Zip/ZipArchive.h"
#include "Poco/Zip/ZipStream.h"

#include "ionclaw/util/HttpClient.hpp"
#include "spdlog/spdlog.h"

namespace fs = std::filesystem;

namespace ionclaw
{
namespace server
{

// marketplace base url
const char *Routes::MARKETPLACE_BASE = "https://ionclaw.com";

void Routes::handleMarketplaceTargets(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp)
{
    nlohmann::json arr = nlohmann::json::array();
    arr.push_back({{"label", "Project"}, {"value", ""}});

    for (const auto &[name, _] : config->agents)
    {
        arr.push_back({{"label", name}, {"value", name}});
    }

    sendJson(resp, arr);
}

namespace
{

// validate marketplace path segments contain only safe characters
bool isValidMarketplaceSegment(const std::string &s)
{
    if (s.empty())
    {
        return false;
    }

    for (char c : s)
    {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '.')
        {
            return false;
        }
    }

    // reject hidden names and parent traversal
    return s[0] != '.' && s.find("..") == std::string::npos;
}

} // namespace

void Routes::handleMarketplaceCheck(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp,
                                    const std::string &source, const std::string &name)
{
    if (!isValidMarketplaceSegment(source) || !isValidMarketplaceSegment(name))
    {
        sendError(resp, "Invalid source or name", 400);
        return;
    }

    // extract agent query parameter
    std::string agent;
    Poco::URI parsedUri(req.getURI());

    for (const auto &[key, value] : parsedUri.getQueryParameters())
    {
        if (key == "agent")
        {
            agent = value;
            break;
        }
    }

    // resolve skill path based on agent
    std::string skillPath;

    if (agent.empty())
    {
        skillPath = config->projectPath + "/skills/" + source + "/" + name + "/SKILL.md";
    }
    else
    {
        auto it = config->agents.find(agent);

        if (it == config->agents.end())
        {
            sendJson(resp, {{"installed", false}});
            return;
        }

        std::string workspace = it->second.workspace;

        if (workspace.empty())
        {
            sendJson(resp, {{"installed", false}});
            return;
        }

        skillPath = workspace + "/skills/" + source + "/" + name + "/SKILL.md";
    }

    bool installed = fs::exists(skillPath) && fs::is_regular_file(skillPath);
    sendJson(resp, {{"installed", installed}});
}

void Routes::handleMarketplaceInstall(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp)
{
    try
    {
        auto body = nlohmann::json::parse(readBody(req));
        std::string source = body.value("source", "");
        std::string name = body.value("name", "");
        std::string agent = body.value("agent", "");

        if (source.empty() || name.empty())
        {
            sendError(resp, "Missing source or name");
            return;
        }

        if (!isValidMarketplaceSegment(source) || !isValidMarketplaceSegment(name))
        {
            sendError(resp, "Invalid source or name", 400);
            return;
        }

        // resolve base directory for skill installation
        std::string baseDir;

        if (agent.empty())
        {
            baseDir = config->projectPath + "/skills";
        }
        else
        {
            auto it = config->agents.find(agent);

            if (it == config->agents.end())
            {
                sendError(resp, "Unknown agent", 404);
                return;
            }

            if (it->second.workspace.empty())
            {
                sendError(resp, "Agent has no workspace");
                return;
            }

            baseDir = it->second.workspace + "/skills";
        }

        // download skill package
        std::string zipUrl = std::string(MARKETPLACE_BASE) + "/skills/" + source + "/" + name + "/package.zip";
        auto response = ionclaw::util::HttpClient::request("GET", zipUrl, {}, "", 60, true);

        if (response.statusCode != 200)
        {
            sendError(resp, "Failed to fetch skill package: HTTP " + std::to_string(response.statusCode), 502);
            return;
        }

        if (response.body.empty())
        {
            sendError(resp, "Skill package is empty", 502);
            return;
        }

        // extract zip to target directory
        fs::path targetDir = fs::path(baseDir) / source / name;
        std::error_code ec;
        fs::create_directories(targetDir, ec);

        fs::path tempZip = fs::temp_directory_path() / ("ionclaw_skill_" + source + "_" + name + ".zip");

        try
        {
            // write zip to temp file
            std::ofstream zipOut(tempZip, std::ios::binary);

            if (!zipOut.is_open())
            {
                sendError(resp, "Failed to write temp file", 500);
                return;
            }

            zipOut.write(response.body.data(), static_cast<std::streamsize>(response.body.size()));
            zipOut.close();

            // open and extract archive
            std::ifstream zipFile(tempZip, std::ios::binary);

            if (!zipFile.is_open())
            {
                fs::remove(tempZip);
                sendError(resp, "Failed to open package", 500);
                return;
            }

            Poco::Zip::ZipArchive archive(zipFile);

            for (auto it = archive.headerBegin(); it != archive.headerEnd(); ++it)
            {
                std::string entryName = it->first;

                // skip directories
                if (entryName.empty() || entryName.back() == '/')
                {
                    continue;
                }

                // strip top-level directory prefix
                size_t slashPos = entryName.find('/');
                std::string relativePath = (slashPos != std::string::npos) ? entryName.substr(slashPos + 1) : entryName;

                if (relativePath.empty())
                {
                    continue;
                }

                fs::path outputPath = targetDir / relativePath;

                // zip slip protection: verify resolved path is within target directory
                auto resolvedOutput = fs::weakly_canonical(outputPath).string();
                auto resolvedTarget = fs::weakly_canonical(targetDir).string();

                if (resolvedOutput.rfind(resolvedTarget, 0) != 0)
                {
                    spdlog::warn("[Marketplace] Zip entry escapes target dir: {}", entryName);
                    continue;
                }

                std::error_code mkdirEc;
                fs::create_directories(outputPath.parent_path(), mkdirEc);

                // re-open archive to seek to entry
                zipFile.clear();
                zipFile.seekg(0);
                Poco::Zip::ZipArchive reArchive(zipFile);
                auto entryIt = reArchive.headerBegin();

                while (entryIt != reArchive.headerEnd() && entryIt->first != entryName)
                {
                    ++entryIt;
                }

                if (entryIt == reArchive.headerEnd())
                {
                    continue;
                }

                // extract entry content
                zipFile.clear();
                zipFile.seekg(0);
                Poco::Zip::ZipInputStream zipStream(zipFile, entryIt->second);
                std::ofstream outFile(outputPath.string(), std::ios::binary);

                if (outFile.is_open())
                {
                    char buffer[8192];

                    while (zipStream.good())
                    {
                        zipStream.read(buffer, sizeof(buffer));
                        auto bytesRead = zipStream.gcount();

                        if (bytesRead > 0)
                        {
                            outFile.write(buffer, bytesRead);
                        }
                    }
                }
            }

            zipFile.close();
            fs::remove(tempZip);
        }
        catch (const std::exception &e)
        {
            if (fs::exists(tempZip))
            {
                fs::remove(tempZip);
            }

            sendError(resp, std::string("Extract failed: ") + e.what(), 500);
            return;
        }

        sendJson(resp, nlohmann::json::object());
    }
    catch (const std::exception &e)
    {
        sendError(resp, e.what(), 500);
    }
}

} // namespace server
} // namespace ionclaw
