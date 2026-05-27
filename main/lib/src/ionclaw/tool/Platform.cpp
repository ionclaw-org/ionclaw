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
// check tv and watch before ios: TARGET_OS_IOS is 0 on those, and the simulator sets TARGET_OS_SIMULATOR too
#if TARGET_OS_TV
    return "tvos";
#elif TARGET_OS_WATCH
    return "watchos";
#elif TARGET_OS_IOS
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
