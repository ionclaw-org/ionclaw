#pragma once

#include <string>

#include "ionclaw/util/Platform.hpp"

namespace ionclaw
{
namespace util
{

struct ProcessResult
{
    std::string output;
    int exitCode = -1;
    bool timedOut = false;
};

class ProcessRunner
{
public:
    static ProcessResult run(const std::string &command, int timeoutSeconds, size_t maxOutputBytes);

private:
    static constexpr size_t BUFFER_SIZE = 4096;
    static constexpr int GRACE_PERIOD_MS = 500;
    static constexpr int GRACE_POLL_MS = 10;

    static bool appendOutput(ProcessResult &result, const char *data, size_t length, size_t maxOutputBytes);

#if !defined(_WIN32) && !defined(IONCLAW_NO_PROCESS_EXEC)
    static void drainPipe(int fd, ProcessResult &result, size_t maxOutputBytes);
    static void collectExitStatus(ProcessResult &result, int status);
#endif
};

} // namespace util
} // namespace ionclaw
