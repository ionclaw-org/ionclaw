#pragma once

#ifdef IONCLAW_HAS_LLAMA_CPP

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "ionclaw/embedding/EmbeddingProvider.hpp"

struct llama_model;
struct llama_context;

namespace ionclaw
{
namespace embedding
{

class LlamaEmbeddingProvider final : public EmbeddingProvider
{
public:
    explicit LlamaEmbeddingProvider(const std::string &providerName);
    ~LlamaEmbeddingProvider() override;

    LlamaEmbeddingProvider(const LlamaEmbeddingProvider &) = delete;
    LlamaEmbeddingProvider &operator=(const LlamaEmbeddingProvider &) = delete;

    std::string providerName() const override;
    std::vector<std::vector<float>> embed(const std::vector<std::string> &texts, const EmbeddingContext &context) const override;

private:
    std::string name;

    mutable std::mutex mutex;
    mutable std::string loadedModelPath;
    mutable llama_model *model = nullptr;
    mutable llama_context *ctx = nullptr;
    mutable int32_t nEmbd = 0;

    void ensureModel(const std::string &modelPath) const;
    void release() const;
    std::vector<float> embedOne(const std::string &text) const;

    static std::string resolveModelPath(const EmbeddingContext &context);
};

} // namespace embedding
} // namespace ionclaw

#endif // IONCLAW_HAS_LLAMA_CPP
