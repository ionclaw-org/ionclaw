#include "ionclaw/server/Routes.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "Poco/Net/HTMLForm.h"
#include "Poco/Net/PartHandler.h"

namespace ionclaw
{
namespace server
{

namespace fs = std::filesystem;

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

    // block hidden and protected paths
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

    std::string fullPath = projectRoot + "/" + rel;

    // canonicalize paths for traversal check
    fs::path canonicalPath;
    fs::path canonicalRoot;

    try
    {
        canonicalPath = fs::weakly_canonical(fs::path(fullPath));
        canonicalRoot = fs::weakly_canonical(fs::path(projectRoot));
    }
    catch (...)
    {
        sendError(resp, "File not found", 404);
        return;
    }

    // verify path is within project root
    std::string rootStr = canonicalRoot.string();

    if (!rootStr.empty() && rootStr.back() != '/' && rootStr.back() != fs::path::preferred_separator)
    {
        rootStr += fs::path::preferred_separator;
    }

    std::string canonicalStr = canonicalPath.string();

    if (canonicalStr != rootStr && canonicalStr.rfind(rootStr, 0) != 0)
    {
        sendError(resp, "Access denied", 403);
        return;
    }

    if (!fs::exists(canonicalPath))
    {
        sendError(resp, "File not found", 404);
        return;
    }

    fullPath = canonicalStr;

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

    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c)
                   { return c < 0x80 ? static_cast<unsigned char>(std::tolower(c)) : c; });

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
        std::string fullPath = projectRoot + "/" + path;

        // verify path is within project root
        fs::path canonicalPath = fs::weakly_canonical(fs::path(fullPath));
        fs::path canonicalRoot = fs::weakly_canonical(fs::path(projectRoot));
        std::string rootStr = canonicalRoot.string();

        if (!rootStr.empty() && rootStr.back() != fs::path::preferred_separator)
        {
            rootStr += fs::path::preferred_separator;
        }

        if (canonicalPath.string().rfind(rootStr, 0) != 0)
        {
            sendError(resp, "Access denied", 403);
            return;
        }

        fullPath = canonicalPath.string();
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
        sendError(resp, e.what());
    }
}

void Routes::handleFileDelete(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp, const std::string &path)
{
    // block hidden and protected paths
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

    std::string fullPath = projectRoot + "/" + path;

    // verify path is within project root
    try
    {
        fs::path canonicalPath = fs::weakly_canonical(fs::path(fullPath));
        fs::path canonicalRoot = fs::weakly_canonical(fs::path(projectRoot));
        std::string rootStr = canonicalRoot.string();

        if (!rootStr.empty() && rootStr.back() != fs::path::preferred_separator)
        {
            rootStr += fs::path::preferred_separator;
        }

        if (canonicalPath.string().rfind(rootStr, 0) != 0)
        {
            sendError(resp, "Access denied", 403);
            return;
        }

        fullPath = canonicalPath.string();
    }
    catch (...)
    {
        sendError(resp, "File not found", 404);
        return;
    }

    if (!fs::exists(fullPath))
    {
        sendError(resp, "File not found", 404);
        return;
    }

    fs::remove_all(fullPath);
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

    std::string fullPath = projectRoot + "/" + path;

    // verify path is within project root
    fs::path canonicalPath = fs::weakly_canonical(fs::path(fullPath));
    fs::path canonicalRoot = fs::weakly_canonical(fs::path(projectRoot));
    std::string rootStr = canonicalRoot.string();

    if (!rootStr.empty() && rootStr.back() != fs::path::preferred_separator)
    {
        rootStr += fs::path::preferred_separator;
    }

    if (canonicalPath.string().rfind(rootStr, 0) != 0)
    {
        sendError(resp, "Access denied", 403);
        return;
    }

    std::error_code ec;
    fs::create_directories(canonicalPath.string(), ec);

    if (ec)
    {
        sendError(resp, "Failed to create directory: " + ec.message());
        return;
    }

    sendJson(resp, {{"status", "ok"}, {"path", path}});
}

void Routes::handleFileCreate(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp, const std::string &path)
{
    // block hidden and protected paths
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

    std::string fullPath = projectRoot + "/" + path;

    fs::path canonicalPath;
    fs::path canonicalRoot;

    try
    {
        canonicalPath = fs::weakly_canonical(fs::path(fullPath));
        canonicalRoot = fs::weakly_canonical(fs::path(projectRoot));
    }
    catch (...)
    {
        sendError(resp, "Invalid path", 400);
        return;
    }

    // verify path is within project root
    std::string rootStr = canonicalRoot.string();

    if (!rootStr.empty() && rootStr.back() != fs::path::preferred_separator)
    {
        rootStr += fs::path::preferred_separator;
    }

    if (canonicalPath.string().rfind(rootStr, 0) != 0)
    {
        sendError(resp, "Access denied", 403);
        return;
    }

    fullPath = canonicalPath.string();

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

    std::string fullPath = projectRoot + "/" + rel;

    // canonicalize paths for traversal check
    fs::path canonicalPath;
    fs::path canonicalRoot;

    try
    {
        canonicalPath = fs::weakly_canonical(fs::path(fullPath));
        canonicalRoot = fs::weakly_canonical(fs::path(projectRoot));
    }
    catch (...)
    {
        sendError(resp, "File not found", 404);
        return;
    }

    // verify path is within project root
    std::string canonicalStr = canonicalPath.string();
    std::string rootStr = canonicalRoot.string();

    if (!rootStr.empty() && rootStr.back() != '/' && rootStr.back() != fs::path::preferred_separator)
    {
        rootStr += fs::path::preferred_separator;
    }

    if (canonicalStr != rootStr && canonicalStr.rfind(rootStr, 0) != 0)
    {
        sendError(resp, "Access denied", 403);
        return;
    }

    if (!fs::exists(canonicalPath) || !fs::is_regular_file(canonicalPath))
    {
        sendError(resp, "File not found", 404);
        return;
    }

    fullPath = canonicalStr;

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

    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c)
                   { return c < 0x80 ? static_cast<unsigned char>(std::tolower(c)) : c; });

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
        if (c == '"' || c == '\\' || c < 0x20)
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

        std::string targetDir = path.empty() ? projectRoot : (projectRoot + "/" + path);

        fs::path canonicalPath;
        fs::path canonicalRoot;

        try
        {
            canonicalPath = fs::weakly_canonical(fs::path(targetDir));
            canonicalRoot = fs::weakly_canonical(fs::path(projectRoot));
        }
        catch (...)
        {
            sendError(resp, "Invalid path", 400);
            return;
        }

        // verify path is within project root
        std::string canonicalStr = canonicalPath.string();
        std::string rootStr = canonicalRoot.string();

        if (!rootStr.empty() && rootStr.back() != fs::path::preferred_separator)
        {
            rootStr += fs::path::preferred_separator;
        }

        if (canonicalStr != canonicalRoot.string() && canonicalStr.rfind(rootStr, 0) != 0)
        {
            sendError(resp, "Access denied", 403);
            return;
        }

        targetDir = canonicalStr;

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
            const std::string &dir;
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
