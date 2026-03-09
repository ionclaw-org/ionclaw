#pragma once

#include <cstdio>

namespace ionclaw
{
namespace util
{

// RAII wrapper for popen/pclose to prevent file descriptor leaks on exceptions
class PipeGuard
{
public:
    explicit PipeGuard(const char *command, const char *mode = "r")
        : pipe_(popen(command, mode))
    {
    }

    ~PipeGuard()
    {
        if (pipe_)
        {
            pclose(pipe_);
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

        auto result = pclose(pipe_);
        pipe_ = nullptr;
        return result;
    }

private:
    FILE *pipe_;
};

} // namespace util
} // namespace ionclaw
