#include "ionclaw/server/handler/HttpHelper.hpp"

#include <unordered_map>

namespace ionclaw
{
namespace server
{
namespace handler
{

std::string HttpHelper::contentTypeForExtension(const std::string &ext)
{
    static const std::unordered_map<std::string, std::string> mimeTypes = {
        {"html", "text/html"},
        {"js", "application/javascript"},
        {"css", "text/css"},
        {"json", "application/json"},
        {"png", "image/png"},
        {"jpg", "image/jpeg"},
        {"jpeg", "image/jpeg"},
        {"gif", "image/gif"},
        {"svg", "image/svg+xml"},
        {"ico", "image/x-icon"},
        {"woff", "font/woff"},
        {"woff2", "font/woff2"},
        {"ttf", "font/ttf"},
        {"map", "application/json"},
    };

    auto it = mimeTypes.find(ext);
    return it != mimeTypes.end() ? it->second : "application/octet-stream";
}

void HttpHelper::addCorsHeaders(Poco::Net::HTTPServerResponse &resp)
{
    resp.set("Access-Control-Allow-Origin", "*");
    resp.set("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, PATCH, OPTIONS");
    resp.set("Access-Control-Allow-Headers", "Content-Type, Authorization");
    resp.set("Access-Control-Max-Age", "86400");
}

std::string HttpHelper::extractPathParam(const std::string &path, const std::string &prefix)
{
    // path already comes percent-decoded from Poco::URI::getPath, so no second decode is applied
    if (path.size() > prefix.size() && path.substr(0, prefix.size()) == prefix)
    {
        return path.substr(prefix.size());
    }

    return "";
}

bool HttpHelper::hasTraversalSegment(const std::string &path)
{
    size_t start = 0;

    while (start <= path.size())
    {
        // treat both separators as boundaries so a backslash segment cannot escape on windows
        auto end = path.find_first_of("/\\", start);
        auto segmentLen = (end == std::string::npos ? path.size() : end) - start;

        if (path.compare(start, segmentLen, "..") == 0)
        {
            return true;
        }

        if (end == std::string::npos)
        {
            break;
        }

        start = end + 1;
    }

    return false;
}

} // namespace handler
} // namespace server
} // namespace ionclaw
