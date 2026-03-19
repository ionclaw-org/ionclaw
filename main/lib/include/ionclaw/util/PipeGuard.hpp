#pragma once

#include <cstdio>

#if defined(_WIN32)
#define IONCLAW_POPEN _popen
#define IONCLAW_PCLOSE _pclose
#else
#define IONCLAW_POPEN popen
#define IONCLAW_PCLOSE pclose
#endif

namespace ionclaw
{
namespace util
{

// raii wrapper for popen/pclose to prevent file descriptor leaks on exceptions
class PipeGuard
{
public:
    explicit PipeGuard(const char *command, const char *mode = "r")
        : pipe_(command ? IONCLAW_POPEN(command, mode) : nullptr)
    {
    }

    ~PipeGuard()
    {
        if (pipe_)
        {
            IONCLAW_PCLOSE(pipe_);
        }
    }

    PipeGuard(const PipeGuard &) = delete;
    PipeGuard &operator=(const PipeGuard &) = delete;

    FILE *get() const { return pipe_; }
    explicit operator bool() const { return pipe_ != nullptr; }

    // release ownership and return the exit code from pclose
    int close()
    {
        if (!pipe_)
        {
            return -1;
        }

        auto result = IONCLAW_PCLOSE(pipe_);
        pipe_ = nullptr;
        return result;
    }

private:
    FILE *pipe_;
};

} // namespace util
} // namespace ionclaw
