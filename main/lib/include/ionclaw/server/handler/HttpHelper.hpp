#pragma once

#include <string>

#include "Poco/Net/HTTPServerResponse.h"

namespace ionclaw
{
namespace server
{
namespace handler
{

class HttpHelper
{
public:
    static std::string contentTypeForExtension(const std::string &ext);
    static void addCorsHeaders(Poco::Net::HTTPServerResponse &resp);
    static std::string extractPathParam(const std::string &path, const std::string &prefix);
    static bool hasTraversalSegment(const std::string &path);
};

} // namespace handler
} // namespace server
} // namespace ionclaw
