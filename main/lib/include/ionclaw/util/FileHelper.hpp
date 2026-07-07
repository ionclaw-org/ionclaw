#pragma once

#include <string>

namespace ionclaw
{
namespace util
{

class FileHelper
{
public:
    // writes content to a temp file in the target directory then renames it over the target, so a crash never leaves a torn file
    // returns an empty string on success or a human-readable error message on failure
    static std::string atomicWrite(const std::string &path, const std::string &content);
};

} // namespace util
} // namespace ionclaw
