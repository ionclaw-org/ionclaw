#include "ionclaw/server/handler/PublicFileHandler.hpp"

#include <filesystem>
#include <fstream>

#include "Poco/Net/HTTPServerRequest.h"
#include "Poco/Net/HTTPServerResponse.h"
#include "Poco/Path.h"
#include "Poco/URI.h"

#include "ionclaw/server/handler/HttpHelper.hpp"

namespace ionclaw
{
namespace server
{
namespace handler
{

namespace fs = std::filesystem;

PublicFileHandler::PublicFileHandler(const std::string &publicDir)
    : publicDir(publicDir)
{
}

void PublicFileHandler::handleRequest(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp)
{
    HttpHelper::addCorsHeaders(resp);

    // resolve request path
    Poco::URI uri(req.getURI());
    auto path = uri.getPath();

    // strip /public prefix
    if (path.size() < 7)
    {
        resp.setStatus(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST);
        resp.setContentType("text/plain");
        auto &out = resp.send();
        out << "Bad Request";
        return;
    }

    path = path.substr(7);

    if (path.empty())
    {
        path = "/";
    }

    // use canonical path comparison to prevent traversal attacks
    auto fullPath = fs::path(publicDir) / path.substr(1);
    auto canonicalRoot = fs::weakly_canonical(fs::path(publicDir));

    fs::path canonicalPath;

    try
    {
        canonicalPath = fs::weakly_canonical(fullPath);
    }
    catch (const std::exception &)
    {
        resp.setStatus(Poco::Net::HTTPResponse::HTTP_NOT_FOUND);
        resp.setContentType("text/plain");
        auto &out = resp.send();
        out << "Not Found";
        return;
    }

    // ensure the resolved path is within the public directory
    auto rootStr = canonicalRoot.string();

    if (!rootStr.empty() && rootStr.back() != '/')
    {
        rootStr += '/';
    }

    if (canonicalPath.string().find(rootStr) != 0 && canonicalPath != canonicalRoot)
    {
        resp.setStatus(Poco::Net::HTTPResponse::HTTP_FORBIDDEN);
        resp.setContentType("text/plain");
        auto &out = resp.send();
        out << "Forbidden";
        return;
    }

    // serve file
    std::ifstream ifs(canonicalPath.string(), std::ios::binary);

    if (ifs.good())
    {
        auto ext = Poco::Path(canonicalPath.string()).getExtension();
        resp.setContentType(HttpHelper::contentTypeForExtension(ext));
        resp.setStatus(Poco::Net::HTTPResponse::HTTP_OK);
        auto &out = resp.send();
        out << ifs.rdbuf();
        return;
    }

    resp.setStatus(Poco::Net::HTTPResponse::HTTP_NOT_FOUND);
    resp.setContentType("text/plain");
    auto &out = resp.send();
    out << "Not Found";
}

} // namespace handler
} // namespace server
} // namespace ionclaw
