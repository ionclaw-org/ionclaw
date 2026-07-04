#include "ionclaw/provider/ClaudeCliProvider.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif !defined(IONCLAW_NO_PROCESS_EXEC)
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "ionclaw/provider/ProviderHelper.hpp"
#include "ionclaw/util/StringHelper.hpp"
#include "ionclaw/util/UniqueId.hpp"
#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace provider
{

namespace
{

// api credentials are removed from the child environment so the cli falls back to the logged-in session
const std::array<const char *, 10> SCRUBBED_ENV_KEYS = {
    "ANTHROPIC_API_KEY",
    "ANTHROPIC_BASE_URL",
    "ANTHROPIC_MODEL",
    "OPENAI_API_KEY",
    "OPENAI_BASE_URL",
    "OPENAI_ORG_ID",
    "OPENAI_PROJECT",
    "GOOGLE_API_KEY",
    "GEMINI_API_KEY",
    "GOOGLE_APPLICATION_CREDENTIALS",
};

constexpr size_t MAX_OUTPUT_BYTES = 8 * 1024 * 1024;
constexpr size_t BUFFER_SIZE = 4096;

struct CliResult
{
    std::string output;
    int exitCode = -1;
    bool timedOut = false;
    bool cancelled = false;
};

// writes the prompt to a private temp file and removes it when the invocation ends
class TempPromptFile
{
public:
    explicit TempPromptFile(const std::string &content)
        : path(std::filesystem::temp_directory_path() / ("ionclaw-claude-" + util::UniqueId::uuid() + ".txt"))
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);

        if (!file)
        {
            throw std::runtime_error("[ClaudeCliProvider] Failed to create prompt file: " + path.string());
        }

        file.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    ~TempPromptFile()
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    TempPromptFile(const TempPromptFile &) = delete;
    TempPromptFile &operator=(const TempPromptFile &) = delete;

    const std::filesystem::path &get() const
    {
        return path;
    }

private:
    std::filesystem::path path;
};

#ifdef _WIN32

std::string resolveBinary(const std::string &name)
{
    const char *pathEnv = std::getenv("PATH");

    if (!pathEnv)
    {
        return "";
    }

    static const std::array<const char *, 4> suffixes = {"", ".exe", ".cmd", ".bat"};
    std::string paths(pathEnv);
    size_t start = 0;

    while (start <= paths.size())
    {
        auto end = paths.find(';', start);
        auto dir = paths.substr(start, end == std::string::npos ? std::string::npos : end - start);
        start = end == std::string::npos ? paths.size() + 1 : end + 1;

        if (dir.empty())
        {
            continue;
        }

        for (const auto *suffix : suffixes)
        {
            auto candidate = std::filesystem::path(dir) / (name + suffix);

            if (std::filesystem::exists(candidate))
            {
                return candidate.string();
            }
        }
    }

    return "";
}

std::wstring toWide(const std::string &text)
{
    if (text.empty())
    {
        return L"";
    }

    int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), count);

    return wide;
}

// compares a wide environment key against an ascii name without allocating
bool asciiKeyEquals(const wchar_t *key, size_t keyLen, const char *name)
{
    size_t i = 0;

    for (; i < keyLen && name[i]; ++i)
    {
        if (key[i] != static_cast<wchar_t>(static_cast<unsigned char>(name[i])))
        {
            return false;
        }
    }

    return i == keyLen && name[i] == '\0';
}

std::wstring buildCommandLine(const std::string &binaryPath, const std::vector<std::string> &args)
{
    // quote each argument so spaces and special characters survive the command line
    // clang-format off
    auto quote = [](const std::wstring &value) {
        std::wstring quoted = L"\"";

        for (wchar_t c : value)
        {
            if (c == L'"' || c == L'\\')
            {
                quoted += L'\\';
            }

            quoted += c;
        }

        quoted += L"\"";
        return quoted;
    };
    // clang-format on

    std::wstring line = quote(toWide(binaryPath));

    for (const auto &arg : args)
    {
        line += L" " + quote(toWide(arg));
    }

    return line;
}

std::wstring buildScrubbedEnvironment()
{
    // build a unicode environment block minus the scrubbed keys from the process environment
    LPWCH block = GetEnvironmentStringsW();

    if (!block)
    {
        return L"";
    }

    std::wstring result;

    for (LPWCH entry = block; *entry; entry += wcslen(entry) + 1)
    {
        auto sep = wcschr(entry, L'=');
        auto keyLen = sep ? static_cast<size_t>(sep - entry) : wcslen(entry);

        bool scrubbed = false;

        for (const auto *key : SCRUBBED_ENV_KEYS)
        {
            if (asciiKeyEquals(entry, keyLen, key))
            {
                scrubbed = true;
                break;
            }
        }

        if (!scrubbed)
        {
            result.append(entry);
            result.push_back(L'\0');
        }
    }

    // a double null terminates the block; an all-scrubbed env still needs the trailing null
    result.push_back(L'\0');
    FreeEnvironmentStringsW(block);

    return result;
}

CliResult runClaude(const std::string &binaryPath, const std::vector<std::string> &args, const std::filesystem::path &promptPath, int timeoutSeconds, const CancelPredicate &isCancelled)
{
    CliResult result;

    // open the prompt file as the child's stdin so large prompts never deadlock a pipe
    HANDLE stdinHandle = CreateFileW(promptPath.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (stdinHandle == INVALID_HANDLE_VALUE)
    {
        result.output = "Error: failed to open prompt file";
        return result;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;

    if (!CreatePipe(&readPipe, &writePipe, &sa, 0))
    {
        CloseHandle(stdinHandle);
        result.output = "Error: failed to create pipe";
        return result;
    }

    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stdinHandle, HANDLE_FLAG_INHERIT, TRUE);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = stdinHandle;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;

    auto scrubbedEnv = buildScrubbedEnvironment();
    auto cmdLine = buildCommandLine(binaryPath, args);

    PROCESS_INFORMATION pi{};

    if (!CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, TRUE, CREATE_UNICODE_ENVIRONMENT, scrubbedEnv.data(), nullptr, &si, &pi))
    {
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        CloseHandle(stdinHandle);
        result.output = "Error: failed to launch the claude cli";
        return result;
    }

    CloseHandle(writePipe);
    CloseHandle(stdinHandle);

    auto deadline = GetTickCount64() + static_cast<ULONGLONG>(timeoutSeconds) * 1000;
    std::array<char, BUFFER_SIZE> buffer;
    DWORD bytesRead = 0;

    while (true)
    {
        auto remaining = static_cast<LONGLONG>(deadline - GetTickCount64());
        bool cancelRequested = isCancelled && isCancelled();

        if (remaining <= 0 || cancelRequested)
        {
            TerminateProcess(pi.hProcess, 1);
            result.cancelled = cancelRequested;
            result.timedOut = !cancelRequested;
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
                while (ReadFile(readPipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) && bytesRead > 0)
                {
                    result.output.append(buffer.data(), bytesRead);
                }

                break;
            }

            continue;
        }

        if (ReadFile(readPipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) && bytesRead > 0)
        {
            result.output.append(buffer.data(), bytesRead);

            if (result.output.size() > MAX_OUTPUT_BYTES)
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

#elif defined(IONCLAW_NO_PROCESS_EXEC) // tvOS, watchOS (no fork/exec)

std::string resolveBinary(const std::string &)
{
    return "";
}

CliResult runClaude(const std::string &, const std::vector<std::string> &, const std::filesystem::path &, int, const CancelPredicate &)
{
    CliResult result;
    result.output = "Error: process execution is not available on this platform";

    return result;
}

#else // POSIX (Linux, macOS)

extern "C" char **environ;

// keeps every current environment entry except the scrubbed credentials, pointing at the existing strings
std::vector<char *> buildScrubbedEnvp()
{
    std::vector<char *> envp;

    for (char **entry = environ; entry && *entry; ++entry)
    {
        std::string variable(*entry);
        auto eq = variable.find('=');
        auto keyName = eq == std::string::npos ? variable : variable.substr(0, eq);

        bool scrubbed = false;

        for (const auto *key : SCRUBBED_ENV_KEYS)
        {
            if (keyName == key)
            {
                scrubbed = true;
                break;
            }
        }

        if (!scrubbed)
        {
            envp.push_back(*entry);
        }
    }

    envp.push_back(nullptr);
    return envp;
}

std::string resolveBinary(const std::string &name)
{
    const char *pathEnv = std::getenv("PATH");

    if (!pathEnv)
    {
        return "";
    }

    std::string paths(pathEnv);
    size_t start = 0;

    while (start <= paths.size())
    {
        auto end = paths.find(':', start);
        auto dir = paths.substr(start, end == std::string::npos ? std::string::npos : end - start);
        start = end == std::string::npos ? paths.size() + 1 : end + 1;

        if (dir.empty())
        {
            continue;
        }

        auto candidate = std::filesystem::path(dir) / name;

        if (::access(candidate.c_str(), X_OK) == 0)
        {
            return candidate.string();
        }
    }

    return "";
}

CliResult runClaude(const std::string &binaryPath, const std::vector<std::string> &args, const std::filesystem::path &promptPath, int timeoutSeconds, const CancelPredicate &isCancelled)
{
    CliResult result;

    // open the prompt file as the child's stdin so large prompts never deadlock a pipe
    int stdinFd = ::open(promptPath.c_str(), O_RDONLY);

    if (stdinFd == -1)
    {
        result.output = "Error: failed to open prompt file";
        return result;
    }

    int outPipe[2];

    if (pipe(outPipe) == -1)
    {
        ::close(stdinFd);
        result.output = "Error: failed to create pipe";
        return result;
    }

    // build argv and the scrubbed environment before forking so the child only calls async-signal-safe functions
    std::vector<char *> argv;
    argv.push_back(const_cast<char *>(binaryPath.c_str()));

    for (const auto &arg : args)
    {
        argv.push_back(const_cast<char *>(arg.c_str()));
    }

    argv.push_back(nullptr);

    auto envp = buildScrubbedEnvp();

    pid_t pid = fork();

    if (pid == -1)
    {
        ::close(stdinFd);
        ::close(outPipe[0]);
        ::close(outPipe[1]);
        result.output = "Error: failed to fork process";
        return result;
    }

    if (pid == 0)
    {
        // child: wire stdio, isolate the process group, exec the cli with the scrubbed environment
        dup2(stdinFd, STDIN_FILENO);
        dup2(outPipe[1], STDOUT_FILENO);
        dup2(outPipe[1], STDERR_FILENO);
        ::close(stdinFd);
        ::close(outPipe[0]);
        ::close(outPipe[1]);

        setsid();
        signal(SIGPIPE, SIG_DFL);

        execve(binaryPath.c_str(), argv.data(), envp.data());
        _exit(127);
    }

    // parent: read stdout with a timeout, killing the process group on timeout or cancel
    ::close(stdinFd);
    ::close(outPipe[1]);

    int flags = fcntl(outPipe[0], F_GETFL, 0);

    if (flags != -1)
    {
        fcntl(outPipe[0], F_SETFL, flags | O_NONBLOCK);
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
    std::array<char, BUFFER_SIZE> buffer;
    bool childExited = false;

    while (true)
    {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        bool cancelRequested = isCancelled && isCancelled();

        if (remaining.count() <= 0 || cancelRequested)
        {
            kill(-pid, SIGKILL);
            waitpid(pid, nullptr, 0);
            result.cancelled = cancelRequested;
            result.timedOut = !cancelRequested;
            childExited = true;
            break;
        }

        struct pollfd pfd{};
        pfd.fd = outPipe[0];
        pfd.events = POLLIN;

        int pollMs = std::min(static_cast<int>(remaining.count()), 100);
        int ready = poll(&pfd, 1, pollMs);

        if (ready < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            break;
        }

        if (ready > 0 && (pfd.revents & (POLLIN | POLLHUP)))
        {
            ssize_t n = read(outPipe[0], buffer.data(), buffer.size());

            if (n > 0)
            {
                result.output.append(buffer.data(), static_cast<size_t>(n));

                if (result.output.size() > MAX_OUTPUT_BYTES)
                {
                    kill(-pid, SIGKILL);
                    waitpid(pid, nullptr, 0);
                    result.exitCode = -1;
                    ::close(outPipe[0]);
                    return result;
                }
            }
            else
            {
                // eof: pipe closed by the child
                break;
            }
        }
        else if (ready == 0)
        {
            int status = 0;

            if (waitpid(pid, &status, WNOHANG) == pid)
            {
                childExited = true;

                ssize_t n = 0;

                while ((n = read(outPipe[0], buffer.data(), buffer.size())) > 0)
                {
                    result.output.append(buffer.data(), static_cast<size_t>(n));
                }

                if (WIFEXITED(status))
                {
                    result.exitCode = WEXITSTATUS(status);
                }

                break;
            }
        }
    }

    ::close(outPipe[0]);

    if (!childExited)
    {
        int status = 0;

        if (waitpid(pid, &status, 0) == pid && WIFEXITED(status))
        {
            result.exitCode = WEXITSTATUS(status);
        }
    }

    return result;
}

#endif

} // namespace

ClaudeCliProvider::ClaudeCliProvider(const std::string &model, int timeout)
    : model(model)
    , timeout(timeout)
{
}

std::string ClaudeCliProvider::name() const
{
    return "claude-cli";
}

std::string ClaudeCliProvider::flattenMessages(const ChatCompletionRequest &request) const
{
    std::string prompt;

    for (const auto &msg : request.messages)
    {
        std::string text = msg.content;

        // pull text out of structured content blocks when the flat field is empty
        if (text.empty() && msg.contentBlocks.is_array())
        {
            for (const auto &block : msg.contentBlocks)
            {
                if (block.value("type", "") == "text")
                {
                    text += block.value("text", "");
                }
            }
        }

        if (text.empty())
        {
            continue;
        }

        std::string role = msg.role;

        for (auto &c : role)
        {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }

        if (!prompt.empty())
        {
            prompt += "\n\n";
        }

        prompt += role + ":\n" + text;
    }

    return prompt;
}

ChatCompletionResponse ClaudeCliProvider::invokeCli(const std::string &prompt, const CancelPredicate &isCancelled) const
{
    auto binaryPath = resolveBinary("claude");

    if (binaryPath.empty())
    {
        throw std::runtime_error("[ClaudeCliProvider] claude cli not found in PATH, install and authenticate it first");
    }

    // single turn, no tools, json output so the subscription answers instead of the api
    std::vector<std::string> args = {
        "--print",
        "--model",
        model,
        "--output-format",
        "json",
        "--max-turns",
        "1",
        "--no-session-persistence",
        "--disallowed-tools",
        "*",
    };

    TempPromptFile promptFile(prompt);
    auto process = runClaude(binaryPath, args, promptFile.get(), timeout, isCancelled);

    if (process.cancelled)
    {
        throw std::runtime_error("[ClaudeCliProvider] request cancelled");
    }

    if (process.timedOut)
    {
        throw std::runtime_error("[ClaudeCliProvider] claude cli exceeded the " + std::to_string(timeout) + "s timeout");
    }

    auto output = ionclaw::util::StringHelper::trim(process.output);

    if (process.exitCode != 0)
    {
        auto safe = ProviderHelper::sanitizeErrorMessage(output);
        spdlog::error("[ClaudeCliProvider] cli failed (exit {}): {}", process.exitCode, safe);
        throw std::runtime_error("[ClaudeCliProvider] claude cli failed (exit " + std::to_string(process.exitCode) + "): " + ionclaw::util::StringHelper::utf8SafeTruncate(safe, 500));
    }

    auto json = nlohmann::json::parse(output, nullptr, false);

    if (json.is_discarded() || !json.is_object())
    {
        throw std::runtime_error("[ClaudeCliProvider] claude cli returned invalid JSON output");
    }

    if (json.value("is_error", false))
    {
        auto message = json.value("result", std::string("unknown error"));
        throw std::runtime_error("[ClaudeCliProvider] claude cli reported an error: " + ionclaw::util::StringHelper::utf8SafeTruncate(message, 500));
    }

    ChatCompletionResponse response;
    response.content = json.value("result", "");
    response.finishReason = "stop";

    if (json.contains("usage") && json["usage"].is_object())
    {
        response.usage = json["usage"];
    }

    return response;
}

ChatCompletionResponse ClaudeCliProvider::chat(const ChatCompletionRequest &request)
{
    return invokeCli(flattenMessages(request), {});
}

void ClaudeCliProvider::chatStream(const ChatCompletionRequest &request, StreamCallback callback, const CancelPredicate &isCancelled)
{
    // the cli answers in a single turn, so the full text is delivered as one content chunk
    auto response = invokeCli(flattenMessages(request), isCancelled);

    if (!response.content.empty())
    {
        StreamChunk chunk;
        chunk.type = "content";
        chunk.content = response.content;
        callback(chunk);
    }

    if (response.usage.is_object())
    {
        StreamChunk chunk;
        chunk.type = "usage";
        chunk.usage = response.usage;
        callback(chunk);
    }

    StreamChunk done;
    done.type = "done";
    done.finishReason = response.finishReason.empty() ? "stop" : response.finishReason;
    callback(done);
}

} // namespace provider
} // namespace ionclaw
