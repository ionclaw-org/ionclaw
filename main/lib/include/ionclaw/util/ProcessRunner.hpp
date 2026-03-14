#pragma once

#include <string>

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

// shell command execution with timeout, output capture, and UTF-8 safe truncation
// uses fork/exec/pipe/waitpid on POSIX, CreateProcess on Windows
class ProcessRunner
{
public:
    // run a shell command with timeout (seconds) and max output capture (bytes)
    static ProcessResult run(const std::string &command, int timeoutSeconds, size_t maxOutputBytes);

private:
    static constexpr size_t BUFFER_SIZE = 4096;
    static constexpr int GRACE_PERIOD_MS = 500;
    static constexpr int GRACE_POLL_MS = 10;

    // append data to output, returns true if truncation limit exceeded
    static bool appendOutput(ProcessResult &result, const char *data, size_t length, size_t maxOutputBytes);

#ifndef _WIN32
    // drain remaining data from a file descriptor (non-blocking) into result
    static void drainPipe(int fd, ProcessResult &result, size_t maxOutputBytes);

    // extract exit code from waitpid status
    static void collectExitStatus(ProcessResult &result, int status);
#endif
};

} // namespace util
} // namespace ionclaw
