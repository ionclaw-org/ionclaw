#pragma once

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

// tvos and watchos prohibit fork/exec/spawn, so launching external processes is unavailable there
#if defined(__APPLE__) && (TARGET_OS_TV || TARGET_OS_WATCH)
#define IONCLAW_NO_PROCESS_EXEC 1
#endif
