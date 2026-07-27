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
struct llama_vocab;
struct common_chat_templates;
struct common_chat_templates_inputs;
struct common_chat_params;
struct common_chat_msg;
struct common_chat_parser_params;
struct common_params_sampling;

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

    struct SamplerParams
    {
        int32_t maxTokens = 4096;
        int32_t topK = 40;
        int32_t repeatLastN = 64;
        double temperature = 0.7;
        double topP = 0.95;
        double repeatPenalty = 1.1;
    };

    std::string modelPath;
    int32_t contextSize;
    int32_t gpuLayers;

    llama_model *model = nullptr;
    llama_context *ctx = nullptr;
    common_chat_templates *templates = nullptr;
    std::mutex inferenceMutex;
    std::atomic<bool> aborted{false};

    void ensureModel();
    void ensureContext();
    void ensureTemplates();
    void releaseTemplates();
    void releaseContext();
    void releaseModel();

    GenerationResult generate(const ChatCompletionRequest &request, const StreamCallback *callback, const CancelPredicate &isCancelled);

    static size_t incompleteUtf8Tail(const std::string &text);
    static bool shouldStop(const std::atomic<bool> &aborted, const CancelPredicate &isCancelled);
    static SamplerParams resolveSamplerParams(const ChatCompletionRequest &request);
    static common_chat_templates_inputs buildChatInputs(const ChatCompletionRequest &request);
    static common_params_sampling buildSamplingParams(const common_chat_params &chatParams, const SamplerParams &params, const llama_vocab *vocab);
    static std::vector<int32_t> tokenizePrompt(const llama_vocab *vocab, const std::string &prompt, uint32_t nCtx);
    static bool prefill(llama_context *ctx, std::vector<int32_t> &tokens, const std::atomic<bool> &aborted, const CancelPredicate &isCancelled);
    static void emitContentDeltas(const std::string &generated, common_chat_msg &previous, const common_chat_parser_params &parserParams, std::string &utf8Pending, const StreamCallback &callback);
    static bool stripAdditionalStop(std::string &generated, const std::vector<std::string> &stops);
};

} // namespace provider
} // namespace ionclaw

#endif // IONCLAW_HAS_LLAMA_CPP
