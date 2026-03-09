#pragma once

#include <string>

namespace ionclaw
{
namespace tool
{

class Platform
{
public:
    // returns lowercase OS name: "ios", "android", "macos", "linux", "windows"
    static std::string current();
};

} // namespace tool
} // namespace ionclaw
