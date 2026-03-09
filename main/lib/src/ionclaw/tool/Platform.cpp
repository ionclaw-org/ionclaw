#include "ionclaw/tool/Platform.hpp"

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

namespace ionclaw
{
namespace tool
{

std::string Platform::current()
{
#if defined(__APPLE__)
#if TARGET_OS_IOS || TARGET_OS_SIMULATOR
    return "ios";
#else
    return "macos";
#endif
#elif defined(__ANDROID__)
    return "android";
#elif defined(_WIN32)
    return "windows";
#else
    return "linux";
#endif
}

} // namespace tool
} // namespace ionclaw
