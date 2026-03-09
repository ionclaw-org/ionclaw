#pragma once

#include <string>

namespace ionclaw
{
namespace util
{

class MimeType
{
public:
    static std::string forPath(const std::string &path);
};

} // namespace util
} // namespace ionclaw
