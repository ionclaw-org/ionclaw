#pragma once

namespace ionclaw
{

#ifdef IONCLAW_VERSION_STRING
constexpr const char *VERSION = IONCLAW_VERSION_STRING;
#else
constexpr const char *VERSION = "0.0.0-dev";
#endif

} // namespace ionclaw
