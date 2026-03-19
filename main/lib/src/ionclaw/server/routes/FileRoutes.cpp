#include "ionclaw/server/Routes.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "Poco/Net/HTMLForm.h"
#include "Poco/Net/PartHandler.h"
#include "ionclaw/util/StringHelper.hpp"

namespace ionclaw
{
namespace server
{

namespace fs = std::filesystem;

// resolves a relative file path to a canonical absolute path within projectRoot
std::string Routes::resolveFilePath(const std::string &relativePath) const
{
    std::string fullPath = projectRoot + "/" + relativePath;

    try
    {
        auto canonicalPath = fs::weakly_canonical(fs::path(fullPath));
        auto canonicalRoot = fs::weakly_canonical(fs::path(projectRoot));
        std::string rootStr = canonicalRoot.string();

        if (!rootStr.empty() && rootStr.back() != fs::path::preferred_separator)
        {
            rootStr += fs::path::preferred_separator;
        }

        auto canonicalStr = canonicalPath.string();

        if (canonicalStr != canonicalRoot.string() && canonicalStr.rfind(rootStr, 0) != 0)
        {
            return "";
        }

        return canonicalStr;
    }
    catch (const std::exception &)
    {
        return "";
    }
}

// files that cannot be read, edited, or deleted via the file API
const std::set<std::string> Routes::PROTECTED_FILES = {
    "config.yml",
    "config.yaml",
};

// files hidden from directory listings
const std::set<std::string> Routes::SYSTEM_FILES = {
    "Thumbs.db",
    "desktop.ini",
    ".DS_Store",
};

// check if a filename (just the basename) is a protected config file
bool Routes::isProtectedFile(const std::string &path)
{
    auto name = fs::path(path).filename().string();
    return PROTECTED_FILES.count(name) > 0;
}

// check if any component of the path starts with '.' or is a system file
bool Routes::isHiddenPath(const std::string &path)
{
    auto normalized = fs::path(path).lexically_normal();

    for (const auto &part : normalized)
    {
        auto s = part.string();

        if (s.empty() || s == "." || s == "..")
        {
            continue;
        }

        if (s[0] == '.')
        {
            return true;
        }

        if (SYSTEM_FILES.count(s) > 0)
        {
            return true;
        }
    }

    return false;
}

// check if a single filename is a system file (for directory listing filtering)
bool Routes::isSystemFile(const std::string &name)
{
    return SYSTEM_FILES.count(name) > 0 || (!name.empty() && name[0] == '.');
}

void Routes::handleFilesList(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp)
{
    auto tree = buildFileTreeFromProject(projectRoot, projectRoot);
    sendJson(resp, tree);
}

void Routes::handleFileRead(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp, const std::string &path)
{
    // normalize path
    std::string rel = (path.size() > 0 && path[0] == '/') ? path.substr(1) : path;

    if (isHiddenPath(rel))
    {
        sendError(resp, "Access denied", 403);
        return;
    }

    if (isProtectedFile(rel))
    {
        sendError(resp, "This file can only be edited via Settings", 403);
        return;
    }

    auto fullPath = resolveFilePath(rel);

    if (fullPath.empty())
    {
        sendError(resp, "Access denied", 403);
        return;
    }

    if (!fs::exists(fullPath))
    {
        sendError(resp, "File not found", 404);
        return;
    }

    // return directory listing for directories
    if (fs::is_directory(fullPath))
    {
        nlohmann::json tree = buildFileTree(fullPath, projectRoot);
        sendJson(resp, tree);
        return;
    }

    // extract and normalize file extension
    auto ext = fs::path(fullPath).extension().string();

    if (!ext.empty() && ext[0] == '.')
    {
        ext = ext.substr(1);
    }

    ionclaw::util::StringHelper::toLowerInPlace(ext);

    auto fileType = detectFileType(ext);

    // return text content inline
    if (fileType == "text")
    {
        std::ifstream ifs(fullPath);

        if (!ifs.good())
        {
            sendError(resp, "Cannot read file", 500);
            return;
        }

        std::ostringstream oss;
        oss << ifs.rdbuf();
        sendJson(resp, {{"path", rel}, {"type", "text"}, {"content", oss.str()}});
    }
    else
    {
        // return metadata with download url for binary files
        auto size = static_cast<int64_t>(fs::file_size(fullPath));
        std::string mime = "application/octet-stream";

        if (fileType == "image")
        {
            mime = "image/" + ext;
        }
        else if (fileType == "video")
        {
            mime = "video/" + ext;
        }
        else if (fileType == "audio")
        {
            mime = "audio/" + ext;
        }

        std::string url = (rel.size() >= 7 && rel.compare(0, 7, "public/") == 0) ? "/" + rel : "/api/files/download/" + rel;

        sendJson(resp, {
                           {"path", rel},
                           {"type", fileType},
                           {"mime", mime},
                           {"size", size},
                           {"url", url},
                       });
    }
}

void Routes::handleFileWrite(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &path)
{
    try
    {
        // block hidden and protected paths
        if (isHiddenPath(path))
        {
            sendError(resp, "Access denied", 403);
            return;
        }

        if (isProtectedFile(path))
        {
            sendError(resp, "This file can only be edited via Settings > Advanced", 403);
            return;
        }

        auto body = nlohmann::json::parse(readBody(req));
        auto content = body.value("content", "");

        auto fullPath = resolveFilePath(path);

        if (fullPath.empty())
        {
            sendError(resp, "Access denied", 403);
            return;
        }

        auto parent = fs::path(fullPath).parent_path();

        if (!parent.empty())
        {
            std::error_code ec;
            fs::create_directories(parent, ec);
        }

        std::ofstream ofs(fullPath);
        ofs << content;
        ofs.flush();

        if (!ofs.good())
        {
            sendError(resp, "Failed to write file", 500);
            return;
        }

        sendJson(resp, {{"status", "ok"}, {"path", path}});
    }
    catch (const std::exception &e)
    {
        sendError(resp, e.what(), 500);
    }
}

void Routes::handleFileDelete(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp, const std::string &path)
{
    if (isHiddenPath(path))
    {
        sendError(resp, "Access denied", 403);
        return;
    }

    if (isProtectedFile(path))
    {
        sendError(resp, "This file cannot be deleted", 403);
        return;
    }

    auto fullPath = resolveFilePath(path);

    if (fullPath.empty())
    {
        sendError(resp, "Access denied", 403);
        return;
    }

    if (!fs::exists(fullPath))
    {
        sendError(resp, "File not found", 404);
        return;
    }

    std::error_code removeEc;
    fs::remove_all(fullPath, removeEc);

    if (removeEc)
    {
        sendError(resp, "Failed to delete: " + removeEc.message(), 500);
        return;
    }

    sendJson(resp, {{"status", "deleted"}});
}

void Routes::handleFileMkdir(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp, const std::string &path)
{
    // block hidden paths
    if (isHiddenPath(path))
    {
        sendError(resp, "Access denied", 403);
        return;
    }

    auto fullPath = resolveFilePath(path);

    if (fullPath.empty())
    {
        sendError(resp, "Access denied", 403);
        return;
    }

    std::error_code ec;
    fs::create_directories(fullPath, ec);

    if (ec)
    {
        sendError(resp, "Failed to create directory: " + ec.message(), 500);
        return;
    }

    sendJson(resp, {{"status", "ok"}, {"path", path}});
}

void Routes::handleFileCreate(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp, const std::string &path)
{
    if (isHiddenPath(path))
    {
        sendError(resp, "Access denied", 403);
        return;
    }

    if (isProtectedFile(path))
    {
        sendError(resp, "Access denied", 403);
        return;
    }

    auto fullPath = resolveFilePath(path);

    if (fullPath.empty())
    {
        sendError(resp, "Access denied", 403);
        return;
    }

    if (fs::exists(fullPath))
    {
        sendError(resp, "File already exists");
        return;
    }

    // ensure parent directories exist
    auto parent = fs::path(fullPath).parent_path();

    if (!parent.empty())
    {
        std::error_code ec;
        fs::create_directories(parent, ec);
    }

    std::ofstream ofs(fullPath);
    ofs.flush();

    if (!ofs.good())
    {
        sendError(resp, "Failed to create file", 500);
        return;
    }

    sendJson(resp, {{"status", "ok"}, {"path", path}});
}

void Routes::handleFileRename(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &path)
{
    try
    {
        // block hidden and protected paths
        if (isHiddenPath(path))
        {
            sendError(resp, "Access denied", 403);
            return;
        }

        if (isProtectedFile(path))
        {
            sendError(resp, "This file cannot be renamed", 403);
            return;
        }

        auto body = nlohmann::json::parse(readBody(req));
        auto newName = body.value("name", "");

        if (newName.empty())
        {
            sendError(resp, "New name is required", 400);
            return;
        }

        // reject names with path separators or hidden names
        if (newName.find('/') != std::string::npos || newName.find('\\') != std::string::npos || newName[0] == '.')
        {
            sendError(resp, "Invalid name", 400);
            return;
        }

        if (isProtectedFile(newName))
        {
            sendError(resp, "Cannot use a protected file name", 403);
            return;
        }

        auto fullPath = resolveFilePath(path);

        if (fullPath.empty())
        {
            sendError(resp, "Access denied", 403);
            return;
        }

        // prevent renaming the project root itself
        auto canonicalRoot = fs::weakly_canonical(fs::path(projectRoot));

        if (fullPath == canonicalRoot.string() || fullPath == canonicalRoot.string() + std::string(1, fs::path::preferred_separator))
        {
            sendError(resp, "Cannot rename root directory", 403);
            return;
        }

        if (!fs::exists(fullPath))
        {
            sendError(resp, "File not found", 404);
            return;
        }

        // build destination path (same parent, new name)
        fs::path destPath = fs::path(fullPath).parent_path() / newName;

        // verify destination is within project root
        auto destResolved = resolveFilePath(fs::relative(destPath, canonicalRoot).string());

        if (destResolved.empty())
        {
            sendError(resp, "Access denied", 403);
            return;
        }

        if (fs::exists(destPath))
        {
            // same file — treat as no-op success
            if (fs::equivalent(fs::path(fullPath), destPath))
            {
                auto relPath = fs::relative(fs::path(fullPath), canonicalRoot).string();
                sendJson(resp, {{"status", "ok"}, {"path", relPath}});
                return;
            }

            sendError(resp, "A file or folder with that name already exists", 400);
            return;
        }

        fs::rename(fs::path(fullPath), destPath);

        // compute new relative path
        auto newRelPath = fs::relative(destPath, canonicalRoot).string();

        sendJson(resp, {{"status", "ok"}, {"path", newRelPath}});
    }
    catch (const std::exception &e)
    {
        sendError(resp, e.what(), 500);
    }
}

void Routes::handleFileDownload(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp, const std::string &path)
{
    // normalize path
    std::string rel = (path.size() > 0 && path[0] == '/') ? path.substr(1) : path;

    // block hidden and protected paths
    if (isHiddenPath(rel))
    {
        sendError(resp, "Access denied", 403);
        return;
    }

    if (isProtectedFile(rel))
    {
        sendError(resp, "Access denied", 403);
        return;
    }

    auto fullPath = resolveFilePath(rel);

    if (fullPath.empty())
    {
        sendError(resp, "Access denied", 403);
        return;
    }

    if (!fs::exists(fullPath) || !fs::is_regular_file(fullPath))
    {
        sendError(resp, "File not found", 404);
        return;
    }

    std::ifstream ifs(fullPath, std::ios::binary);

    if (!ifs.good())
    {
        sendError(resp, "Cannot read file", 500);
        return;
    }

    // extract and normalize file extension
    auto ext = fs::path(fullPath).extension().string();

    if (!ext.empty() && ext[0] == '.')
    {
        ext = ext.substr(1);
    }

    ionclaw::util::StringHelper::toLowerInPlace(ext);

    // determine content type from file type
    auto fileType = detectFileType(ext);
    std::string contentType = "application/octet-stream";

    if (fileType == "text")
    {
        contentType = "text/plain";
    }
    else if (fileType == "image")
    {
        contentType = "image/" + ext;
    }
    else if (fileType == "video")
    {
        contentType = "video/" + ext;
    }
    else if (fileType == "audio")
    {
        contentType = "audio/" + ext;
    }

    auto filename = fs::path(fullPath).filename().string();

    // sanitize filename for Content-Disposition header (remove control chars and quotes)
    std::string safeFilename;
    safeFilename.reserve(filename.size());

    for (char c : filename)
    {
        if (c == '"' || c == '\\' || static_cast<unsigned char>(c) < 0x20)
        {
            safeFilename += '_';
        }
        else
        {
            safeFilename += c;
        }
    }

    resp.setStatus(Poco::Net::HTTPResponse::HTTP_OK);
    resp.setContentType(contentType);
    resp.set("Content-Disposition", "attachment; filename=\"" + safeFilename + "\"");
    auto &out = resp.send();
    out << ifs.rdbuf();
}

void Routes::handleFileUpload(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &path)
{
    try
    {
        // block hidden paths
        if (!path.empty() && isHiddenPath(path))
        {
            sendError(resp, "Access denied", 403);
            return;
        }

        std::string targetDir;

        if (path.empty())
        {
            targetDir = fs::weakly_canonical(fs::path(projectRoot)).string();
        }
        else
        {
            targetDir = resolveFilePath(path);

            if (targetDir.empty())
            {
                sendError(resp, "Access denied", 403);
                return;
            }
        }

        std::error_code ec;
        fs::create_directories(targetDir, ec);

        nlohmann::json uploaded = nlohmann::json::array();

        class UploadHandler : public Poco::Net::PartHandler
        {
        public:
            UploadHandler(const std::string &dir, nlohmann::json &uploaded)
                : dir(dir)
                , uploaded(uploaded)
            {
            }

            void handlePart(const Poco::Net::MessageHeader &header, std::istream &stream) override
            {
                auto disp = header.get("Content-Disposition", "");
                auto namePos = disp.find("filename=\"");
                std::string filename = "upload.bin";

                if (namePos != std::string::npos)
                {
                    auto start = namePos + 10;
                    auto end = disp.find('"', start);

                    if (end != std::string::npos)
                    {
                        filename = disp.substr(start, end - start);
                    }
                }

                // strip directory components for safety
                auto basename = fs::path(filename).filename().string();

                if (basename.empty())
                {
                    basename = "upload.bin";
                }

                // skip hidden files
                if (basename[0] == '.')
                {
                    return;
                }

                // skip protected config files
                if (Routes::isProtectedFile(basename))
                {
                    return;
                }

                auto fullPath = dir + "/" + basename;

                std::ofstream ofs(fullPath, std::ios::binary);
                ofs << stream.rdbuf();
                ofs.flush();

                if (ofs.good())
                {
                    uploaded.push_back(basename);
                }
            }

        private:
            std::string dir;
            nlohmann::json &uploaded;
        };

        UploadHandler handler(targetDir, uploaded);
        Poco::Net::HTMLForm form(req, req.stream(), handler);

        sendJson(resp, {{"status", "ok"}, {"paths", uploaded}});
    }
    catch (const std::exception &e)
    {
        sendError(resp, e.what(), 500);
    }
}

} // namespace server
} // namespace ionclaw
