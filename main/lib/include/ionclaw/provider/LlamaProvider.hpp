#pragma once

#ifdef IONCLAW_HAS_LLAMA_CPP

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "ionclaw/provider/LlmProvider.hpp"

struct llama_model;
struct llama_context;
struct common_chat_templates;

namespace ionclaw
{
namespace provider
{

class LlamaProvider final : public LlmProvider
{
public:
    LlamaProvider(const std::string &modelPath, const nlohmann::json &params = nlohmann::json::object());
    ~LlamaProvider() override;

    LlamaProvider(const LlamaProvider &) = delete;
    LlamaProvider &operator=(const LlamaProvider &) = delete;
    LlamaProvider(LlamaProvider &&) = delete;
    LlamaProvider &operator=(LlamaProvider &&) = delete;

    ChatCompletionResponse chat(const ChatCompletionRequest &request) override;
    void chatStream(const ChatCompletionRequest &request, StreamCallback callback, const CancelPredicate &isCancelled = {}) override;
    std::string name() const override;

private:
    struct GenerationResult
    {
        std::string content;
        std::string reasoningContent;
        std::vector<ToolCall> toolCalls;
        std::string finishReason;
        int promptTokens = 0;
        int completionTokens = 0;
    };

    std::string modelPath;
    int32_t contextSize;
    int32_t gpuLayers;

    llama_model *model = nullptr;
    llama_context *ctx = nullptr;
    common_chat_templates *templates = nullptr;
    std::mutex inferenceMutex;
    std::atomic<bool> aborted{false};

    static std::mutex backendMutex;
    static int backendRefCount;

    void ensureModel();
    void ensureContext();
    void ensureTemplates();
    void releaseTemplates();
    void releaseContext();
    void releaseModel();

    GenerationResult generate(const ChatCompletionRequest &request, const StreamCallback *callback, const CancelPredicate &isCancelled);

    static void acquireBackend();
    static void releaseBackend();
};

} // namespace provider
} // namespace ionclaw

#endif // IONCLAW_HAS_LLAMA_CPP
