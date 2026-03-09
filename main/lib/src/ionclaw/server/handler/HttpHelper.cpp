#include "ionclaw/server/handler/HttpHelper.hpp"

namespace ionclaw
{
namespace server
{
namespace handler
{

// maps file extension to mime content type
std::string HttpHelper::contentTypeForExtension(const std::string &ext)
{
    if (ext == "html")
    {
        return "text/html";
    }

    if (ext == "js")
    {
        return "application/javascript";
    }

    if (ext == "css")
    {
        return "text/css";
    }

    if (ext == "json")
    {
        return "application/json";
    }

    if (ext == "png")
    {
        return "image/png";
    }

    if (ext == "jpg" || ext == "jpeg")
    {
        return "image/jpeg";
    }

    if (ext == "gif")
    {
        return "image/gif";
    }

    if (ext == "svg")
    {
        return "image/svg+xml";
    }

    if (ext == "ico")
    {
        return "image/x-icon";
    }

    if (ext == "woff")
    {
        return "font/woff";
    }

    if (ext == "woff2")
    {
        return "font/woff2";
    }

    if (ext == "ttf")
    {
        return "font/ttf";
    }

    if (ext == "map")
    {
        return "application/json";
    }

    return "application/octet-stream";
}

// adds permissive cors headers for cross-origin requests
void HttpHelper::addCorsHeaders(Poco::Net::HTTPServerResponse &resp)
{
    resp.set("Access-Control-Allow-Origin", "*");
    resp.set("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, PATCH, OPTIONS");
    resp.set("Access-Control-Allow-Headers", "Content-Type, Authorization");
    resp.set("Access-Control-Max-Age", "86400");
}

// extracts the path segment after a given prefix
std::string HttpHelper::extractPathParam(const std::string &path, const std::string &prefix)
{
    if (path.size() > prefix.size() && path.substr(0, prefix.size()) == prefix)
    {
        return path.substr(prefix.size());
    }

    return "";
}

} // namespace handler
} // namespace server
} // namespace ionclaw
