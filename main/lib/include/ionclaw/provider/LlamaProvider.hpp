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

class LlamaProvider : public LlmProvider
{
public:
    LlamaProvider(const std::string &modelPath, const nlohmann::json &params = nlohmann::json::object());
    ~LlamaProvider() override;

    LlamaProvider(const LlamaProvider &) = delete;
    LlamaProvider &operator=(const LlamaProvider &) = delete;
    LlamaProvider(LlamaProvider &&) = delete;
    LlamaProvider &operator=(LlamaProvider &&) = delete;

    ChatCompletionResponse chat(const ChatCompletionRequest &request) override;
    void chatStream(const ChatCompletionRequest &request, StreamCallback callback) override;
    std::string name() const override;

private:
    std::string modelPath;
    int32_t contextSize;
    int32_t gpuLayers;

    llama_model *model = nullptr;
    std::mutex inferenceMutex;
    std::atomic<bool> aborted{false};

    // backend lifetime management (shared across all instances)
    static std::mutex backendMutex;
    static int backendRefCount;
    static void acquireBackend();
    static void releaseBackend();

    void ensureModel();
    void releaseModel();

    struct SamplerParams
    {
        int32_t maxTokens = 4096;
        double temperature = 0.7;
        double topP = 0.95;
        int32_t topK = 40;
        double repeatPenalty = 1.1;
    };

    std::string buildPrompt(const ChatCompletionRequest &request) const;
    SamplerParams resolveSamplerParams(const ChatCompletionRequest &request) const;
    std::string generate(const std::string &prompt, const SamplerParams &params,
                         StreamCallback *callback, std::atomic<bool> *cancelled);

    static size_t incompleteUtf8Tail(const std::string &s);
};

} // namespace provider
} // namespace ionclaw

#endif // IONCLAW_HAS_LLAMA_CPP
