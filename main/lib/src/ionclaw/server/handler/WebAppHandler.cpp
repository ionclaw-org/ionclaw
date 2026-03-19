#include "ionclaw/server/handler/WebAppHandler.hpp"

#include <filesystem>
#include <fstream>

#include "Poco/Net/HTTPServerRequest.h"
#include "Poco/Net/HTTPServerResponse.h"
#include "Poco/Path.h"
#include "Poco/URI.h"

#include "ionclaw/server/handler/HttpHelper.hpp"
#include "ionclaw/util/EmbeddedResources.hpp"

namespace ionclaw
{
namespace server
{
namespace handler
{

WebAppHandler::WebAppHandler(const std::string &webDir)
    : webDir(webDir)
{
}

void WebAppHandler::handleRequest(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp)
{
    HttpHelper::addCorsHeaders(resp);

    // resolve request path
    Poco::URI uri(req.getURI());
    auto path = uri.getPath();

    // strip /app prefix
    path = path.substr(4);

    if (path.empty() || path == "/")
    {
        path = "/index.html";
    }

    // strip leading slash for resource lookup
    auto resourcePath = (path[0] == '/') ? path.substr(1) : path;

    // embedded mode: webDir is empty, serve from compiled-in resources
    if (webDir.empty())
    {
        auto [data, size] = ionclaw::util::EmbeddedResources::getWebFile(resourcePath);

        if (data)
        {
            auto ext = Poco::Path(resourcePath).getExtension();
            resp.setContentType(HttpHelper::contentTypeForExtension(ext));
            resp.setStatus(Poco::Net::HTTPResponse::HTTP_OK);
            auto &out = resp.send();
            out.write(data, static_cast<std::streamsize>(size));
            return;
        }

        // spa fallback: serve index.html for client-side routes
        auto [indexData, indexSize] = ionclaw::util::EmbeddedResources::getWebFile("index.html");

        if (indexData)
        {
            resp.setContentType("text/html");
            resp.setStatus(Poco::Net::HTTPResponse::HTTP_OK);
            auto &out = resp.send();
            out.write(indexData, static_cast<std::streamsize>(indexSize));
            return;
        }

        resp.setStatus(Poco::Net::HTTPResponse::HTTP_NOT_FOUND);
        resp.setContentType("text/plain");
        auto &out = resp.send();
        out << "Not Found";
        return;
    }

    // filesystem mode: serve from webDir (with path traversal protection)
    auto filePath = webDir + path;
    std::error_code ecPath, ecRoot;
    auto canonicalPath = std::filesystem::weakly_canonical(filePath, ecPath);
    auto canonicalRoot = std::filesystem::weakly_canonical(webDir, ecRoot);

    if (ecPath || ecRoot || (canonicalPath.string().find(canonicalRoot.string() + "/") != 0 && canonicalPath != canonicalRoot))
    {
        resp.setStatus(Poco::Net::HTTPResponse::HTTP_FORBIDDEN);
        resp.setContentType("text/plain");
        auto &out = resp.send();
        out << "Forbidden";
        return;
    }

    auto resolvedPath = canonicalPath.string();
    std::ifstream ifs(resolvedPath, std::ios::binary);

    if (ifs.good())
    {
        auto ext = Poco::Path(resolvedPath).getExtension();
        resp.setContentType(HttpHelper::contentTypeForExtension(ext));
        resp.setStatus(Poco::Net::HTTPResponse::HTTP_OK);
        auto &out = resp.send();
        out << ifs.rdbuf();
        return;
    }

    // spa fallback: serve index.html for client-side routes
    std::string indexPath = webDir + "/index.html";
    std::ifstream indexIfs(indexPath, std::ios::binary);

    if (indexIfs.good())
    {
        resp.setContentType("text/html");
        resp.setStatus(Poco::Net::HTTPResponse::HTTP_OK);
        auto &out = resp.send();
        out << indexIfs.rdbuf();
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
