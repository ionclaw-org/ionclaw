#pragma once

#ifdef IONCLAW_HAS_LLAMA_CPP

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include "nlohmann/json.hpp"

#include "ionclaw/provider/LlmProvider.hpp"

struct llama_model;

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
    std::mutex inferenceMutex;
    std::atomic<bool> aborted{false};

    static std::mutex backendMutex;
    static int backendRefCount;

    void ensureModel();
    void releaseModel();

    std::string buildPrompt(const ChatCompletionRequest &request) const;
    SamplerParams resolveSamplerParams(const ChatCompletionRequest &request) const;
    std::string generate(const std::string &prompt, const SamplerParams &params, const StreamCallback *callback, const CancelPredicate &isCancelled);

    static void acquireBackend();
    static void releaseBackend();
    static size_t incompleteUtf8Tail(const std::string &text);
};

} // namespace provider
} // namespace ionclaw

#endif // IONCLAW_HAS_LLAMA_CPP
