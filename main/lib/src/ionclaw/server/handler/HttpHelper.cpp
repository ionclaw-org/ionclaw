#include "ionclaw/server/handler/HttpHelper.hpp"

#include <unordered_map>

#include "Poco/URI.h"

namespace ionclaw
{
namespace server
{
namespace handler
{

// maps file extension to mime content type
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

// adds permissive cors headers for cross-origin requests
void HttpHelper::addCorsHeaders(Poco::Net::HTTPServerResponse &resp)
{
    resp.set("Access-Control-Allow-Origin", "*");
    resp.set("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, PATCH, OPTIONS");
    resp.set("Access-Control-Allow-Headers", "Content-Type, Authorization");
    resp.set("Access-Control-Max-Age", "86400");
}

// extracts the path segment after a given prefix, with URL decoding
std::string HttpHelper::extractPathParam(const std::string &path, const std::string &prefix)
{
    if (path.size() > prefix.size() && path.substr(0, prefix.size()) == prefix)
    {
        std::string raw = path.substr(prefix.size());
        std::string decoded;
        Poco::URI::decode(raw, decoded);
        return decoded;
    }

    return "";
}

} // namespace handler
} // namespace server
} // namespace ionclaw
