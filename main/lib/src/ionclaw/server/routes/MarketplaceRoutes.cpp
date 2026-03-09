#include "ionclaw/server/Routes.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

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

void Routes::handleMarketplaceCheck(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp,
                                    const std::string &source, const std::string &name)
{
    // extract agent query parameter
    std::string agent;
    auto uri = req.getURI();
    auto q = uri.find('?');

    if (q != std::string::npos)
    {
        std::string query = uri.substr(q + 1);
        auto pos = query.find("agent=");

        if (pos != std::string::npos)
        {
            agent = query.substr(pos + 6);
            auto amp = agent.find('&');

            if (amp != std::string::npos)
            {
                agent = agent.substr(0, amp);
            }
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
            sendJson(resp, {{"error", "Missing source or name"}});
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
                sendJson(resp, {{"error", "Unknown agent"}});
                return;
            }

            if (it->second.workspace.empty())
            {
                sendJson(resp, {{"error", "Agent has no workspace"}});
                return;
            }

            baseDir = it->second.workspace + "/skills";
        }

        // download skill package
        std::string zipUrl = std::string(MARKETPLACE_BASE) + "/skills/" + source + "/" + name + "/package.zip";
        auto response = ionclaw::util::HttpClient::request("GET", zipUrl, {}, "", 60, true);

        if (response.statusCode != 200)
        {
            sendJson(resp, {{"error", "Failed to fetch skill package: HTTP " + std::to_string(response.statusCode)}});
            return;
        }

        if (response.body.empty())
        {
            sendJson(resp, {{"error", "Skill package is empty"}});
            return;
        }

        // extract zip to target directory
        fs::path targetDir = fs::path(baseDir) / source / name;
        std::error_code ec;
        fs::create_directories(targetDir, ec);

        fs::path tempZip = fs::temp_directory_path() / ("ionclaw-skill-" + source + "-" + name + ".zip");

        try
        {
            // write zip to temp file
            std::ofstream zipOut(tempZip, std::ios::binary);

            if (!zipOut.is_open())
            {
                sendJson(resp, {{"error", "Failed to write temp file"}});
                return;
            }

            zipOut.write(response.body.data(), static_cast<std::streamsize>(response.body.size()));
            zipOut.close();

            // open and extract archive
            std::ifstream zipFile(tempZip, std::ios::binary);

            if (!zipFile.is_open())
            {
                fs::remove(tempZip);
                sendJson(resp, {{"error", "Failed to open package"}});
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

            sendJson(resp, {{"error", std::string("Extract failed: ") + e.what()}});
            return;
        }

        sendJson(resp, nlohmann::json::object());
    }
    catch (const std::exception &e)
    {
        sendJson(resp, {{"error", e.what()}});
    }
}

} // namespace server
} // namespace ionclaw
