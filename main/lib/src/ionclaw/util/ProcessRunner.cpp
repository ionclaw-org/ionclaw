#include "ionclaw/util/ProcessRunner.hpp"

#include <array>
#include <chrono>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "ionclaw/util/StringHelper.hpp"

namespace ionclaw
{
namespace util
{

bool ProcessRunner::appendOutput(ProcessResult &result, const char *data, size_t length, size_t maxOutputBytes)
{
    result.output.append(data, length);

    if (result.output.size() > maxOutputBytes)
    {
        result.output = StringHelper::utf8SafeTruncate(result.output, maxOutputBytes) + "\n... [output truncated]";
        return true;
    }

    return false;
}

#ifdef _WIN32

ProcessResult ProcessRunner::run(const std::string &command, int timeoutSeconds, size_t maxOutputBytes)
{
    ProcessResult result;

    // create pipe for stdout/stderr
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;

    if (!CreatePipe(&readPipe, &writePipe, &sa, 0))
    {
        result.output = "Error: failed to create pipe";
        return result;
    }

    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    std::string cmdLine = "cmd.exe /C " + command;

    if (!CreateProcessA(nullptr, cmdLine.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi))
    {
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        result.output = "Error: failed to execute command";
        return result;
    }

    CloseHandle(writePipe);

    // read output with timeout
    auto deadline = GetTickCount64() + static_cast<ULONGLONG>(timeoutSeconds) * 1000;
    std::array<char, BUFFER_SIZE> buffer;
    DWORD bytesRead = 0;

    while (true)
    {
        auto remaining = static_cast<LONGLONG>(deadline - GetTickCount64());

        if (remaining <= 0)
        {
            TerminateProcess(pi.hProcess, 1);
            result.timedOut = true;
            break;
        }

        DWORD available = 0;

        if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr))
        {
            break;
        }

        if (available == 0)
        {
            if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0)
            {
                // process exited, drain remaining output
                while (ReadFile(readPipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) && bytesRead > 0)
                {
                    if (appendOutput(result, buffer.data(), bytesRead, maxOutputBytes))
                    {
                        break;
                    }
                }

                break;
            }

            continue;
        }

        if (ReadFile(readPipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) && bytesRead > 0)
        {
            if (appendOutput(result, buffer.data(), bytesRead, maxOutputBytes))
            {
                TerminateProcess(pi.hProcess, 1);
                break;
            }
        }
    }

    WaitForSingleObject(pi.hProcess, 5000);

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    result.exitCode = static_cast<int>(exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(readPipe);

    return result;
}

#else // POSIX (Linux, macOS)

void ProcessRunner::collectExitStatus(ProcessResult &result, int status)
{
    if (WIFEXITED(status))
    {
        result.exitCode = WEXITSTATUS(status);
    }
    else if (WIFSIGNALED(status))
    {
        result.exitCode = -WTERMSIG(status);
    }
}

void ProcessRunner::drainPipe(int fd, ProcessResult &result, size_t maxOutputBytes)
{
    std::array<char, BUFFER_SIZE> buffer;

    while (true)
    {
        ssize_t n = read(fd, buffer.data(), buffer.size());

        if (n <= 0)
        {
            break;
        }

        if (appendOutput(result, buffer.data(), static_cast<size_t>(n), maxOutputBytes))
        {
            break;
        }
    }
}

ProcessResult ProcessRunner::run(const std::string &command, int timeoutSeconds, size_t maxOutputBytes)
{
    ProcessResult result;

    // create pipe for stdout/stderr
    int pipefd[2];

    if (pipe(pipefd) == -1)
    {
        result.output = "Error: failed to create pipe";
        return result;
    }

    pid_t pid = fork();

    if (pid == -1)
    {
        close(pipefd[0]);
        close(pipefd[1]);
        result.output = "Error: failed to fork process";
        return result;
    }

    if (pid == 0)
    {
        // child: redirect stdout/stderr to pipe, exec via shell
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        // create new process group so we can kill the entire tree
        setsid();

        // reset signal handlers to default
        signal(SIGPIPE, SIG_DFL);

        execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
        _exit(127);
    }

    // parent: read from pipe with timeout
    close(pipefd[1]);

    // set read end to non-blocking
    int flags = fcntl(pipefd[0], F_GETFL, 0);

    if (flags != -1)
    {
        fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
    std::array<char, BUFFER_SIZE> buffer;
    bool childExited = false;

    while (true)
    {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());

        if (remaining.count() <= 0)
        {
            // timeout: kill process group
            kill(-pid, SIGTERM);

            // brief grace period, then force kill
            int status = 0;
            auto grace = std::chrono::steady_clock::now() + std::chrono::milliseconds(GRACE_PERIOD_MS);

            while (std::chrono::steady_clock::now() < grace)
            {
                if (waitpid(pid, &status, WNOHANG) == pid)
                {
                    childExited = true;
                    break;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(GRACE_POLL_MS));
            }

            if (!childExited)
            {
                kill(-pid, SIGKILL);
                waitpid(pid, &status, 0);
            }

            // drain any output produced before timeout
            drainPipe(pipefd[0], result, maxOutputBytes);

            result.timedOut = true;
            result.exitCode = -1;
            break;
        }

        // poll for readable data
        struct pollfd pfd{};
        pfd.fd = pipefd[0];
        pfd.events = POLLIN;

        int pollMs = std::min(static_cast<int>(remaining.count()), 100);
        int ready = poll(&pfd, 1, pollMs);

        if (ready < 0)
        {
            // EINTR: interrupted by signal, just retry
            if (errno == EINTR)
            {
                continue;
            }

            // other poll error, bail out
            break;
        }

        if (ready > 0)
        {
            if (pfd.revents & (POLLIN | POLLHUP))
            {
                ssize_t n = read(pipefd[0], buffer.data(), buffer.size());

                if (n > 0)
                {
                    if (appendOutput(result, buffer.data(), static_cast<size_t>(n), maxOutputBytes))
                    {
                        kill(-pid, SIGKILL);
                        waitpid(pid, nullptr, 0);
                        result.exitCode = -1;
                        close(pipefd[0]);
                        return result;
                    }
                }
                else
                {
                    // EOF or error: pipe closed
                    break;
                }
            }
            else if (pfd.revents & POLLERR)
            {
                // pipe error, stop reading
                break;
            }
        }
        else if (ready == 0)
        {
            // poll timed out, check if child exited
            int status = 0;

            if (waitpid(pid, &status, WNOHANG) == pid)
            {
                childExited = true;

                // drain any remaining data
                drainPipe(pipefd[0], result, maxOutputBytes);

                collectExitStatus(result, status);
                break;
            }
        }
    }

    close(pipefd[0]);

    // collect exit status if not already done
    if (!childExited && !result.timedOut)
    {
        int status = 0;
        waitpid(pid, &status, 0);
        collectExitStatus(result, status);
    }

    return result;
}

#endif

} // namespace util
} // namespace ionclaw
