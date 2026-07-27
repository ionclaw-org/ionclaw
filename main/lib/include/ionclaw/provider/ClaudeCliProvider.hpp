#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include "ionclaw/provider/LlmProvider.hpp"

namespace ionclaw
{
namespace provider
{

class ClaudeCliProvider final : public LlmProvider
{
public:
    ClaudeCliProvider(const std::string &model, int timeout = 180);

    ChatCompletionResponse chat(const ChatCompletionRequest &request) override;
    void chatStream(const ChatCompletionRequest &request, StreamCallback callback, const CancelPredicate &isCancelled = {}) override;
    std::string name() const override;

private:
    struct CliResult
    {
        std::string output;
        int exitCode = -1;
        bool timedOut = false;
        bool cancelled = false;
    };

    class TempPromptFile
    {
    public:
        explicit TempPromptFile(const std::string &content);
        ~TempPromptFile();

        TempPromptFile(const TempPromptFile &) = delete;
        TempPromptFile &operator=(const TempPromptFile &) = delete;

        const std::filesystem::path &get() const;

    private:
        std::filesystem::path path;
    };

    std::string model;
    int timeout;

    std::string flattenMessages(const ChatCompletionRequest &request) const;
    ChatCompletionResponse invokeCli(const std::string &prompt, const CancelPredicate &isCancelled) const;

    static std::string resolveBinary(const std::string &name);
    static CliResult runClaude(const std::string &binaryPath, const std::vector<std::string> &args, const std::filesystem::path &promptPath, int timeoutSeconds, const CancelPredicate &isCancelled);

    static constexpr std::array<const char *, 10> scrubbedEnvKeys = {
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

    static constexpr size_t MAX_OUTPUT_BYTES = 8 * 1024 * 1024;
    static constexpr size_t BUFFER_SIZE = 4096;

#ifdef _WIN32
    static std::wstring toWide(const std::string &text);
    static bool asciiKeyEquals(const wchar_t *key, size_t keyLen, const char *name);
    static std::wstring buildCommandLine(const std::string &binaryPath, const std::vector<std::string> &args);
    static std::wstring buildScrubbedEnvironment();
#elif !defined(IONCLAW_NO_PROCESS_EXEC)
    static std::vector<char *> buildScrubbedEnvp();
#endif
};

} // namespace provider
} // namespace ionclaw
