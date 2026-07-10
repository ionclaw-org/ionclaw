#ifdef IONCLAW_HAS_LLAMA_CPP

#include "ionclaw/embedding/LlamaEmbeddingProvider.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

#include "llama.h"

#include "common.h"

#include "spdlog/spdlog.h"

#include "ionclaw/config/Config.hpp"

namespace ionclaw
{
namespace embedding
{

namespace
{
constexpr uint32_t EMBEDDING_CONTEXT = 8192;
}

std::mutex LlamaEmbeddingProvider::backendMutex;
int LlamaEmbeddingProvider::backendRefCount = 0;

void LlamaEmbeddingProvider::acquireBackend()
{
    std::lock_guard<std::mutex> lock(backendMutex);

    if (backendRefCount == 0)
    {
        llama_backend_init();
    }

    ++backendRefCount;
}

void LlamaEmbeddingProvider::releaseBackend()
{
    std::lock_guard<std::mutex> lock(backendMutex);

    if (backendRefCount <= 0)
    {
        return;
    }

    if (--backendRefCount == 0)
    {
        llama_backend_free();
    }
}

LlamaEmbeddingProvider::LlamaEmbeddingProvider(const std::string &providerName)
    : name(providerName)
{
    acquireBackend();
}

LlamaEmbeddingProvider::~LlamaEmbeddingProvider()
{
    std::lock_guard<std::mutex> lock(mutex);
    release();
    releaseBackend();
}

std::string LlamaEmbeddingProvider::providerName() const
{
    return name;
}

void LlamaEmbeddingProvider::release() const
{
    if (ctx)
    {
        llama_free(ctx);
        ctx = nullptr;
    }

    if (model)
    {
        llama_model_free(model);
        model = nullptr;
    }

    loadedModelPath.clear();
    nEmbd = 0;
}

std::string LlamaEmbeddingProvider::resolveModelPath(const EmbeddingContext &context)
{
    if (!context.config)
    {
        return "";
    }

    // the model string is "llama/<prefix>", matched against the gguf file names configured as providers
    auto requested = context.model;
    auto slashPos = requested.find('/');
    requested = slashPos != std::string::npos ? requested.substr(slashPos + 1) : std::string();

    for (const auto &[key, provider] : context.config->providers)
    {
        auto path = provider.baseUrl;
        auto sep = path.find_last_of("/\\");
        auto fileName = sep == std::string::npos ? path : path.substr(sep + 1);

        auto lower = fileName;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (lower.size() < 5 || lower.compare(lower.size() - 5, 5, ".gguf") != 0)
        {
            continue;
        }

        if (requested.empty() || lower.rfind(requested, 0) == 0)
        {
            return path;
        }
    }

    return "";
}

void LlamaEmbeddingProvider::ensureModel(const std::string &modelPath) const
{
    if (model && loadedModelPath == modelPath)
    {
        return;
    }

    release();

    spdlog::info("[LlamaEmbeddingProvider] loading model: {}", modelPath);

    auto modelParams = llama_model_default_params();
    model = llama_model_load_from_file(modelPath.c_str(), modelParams);

    if (!model)
    {
        throw std::runtime_error("llama: failed to load embedding model from '" + modelPath + "'");
    }

    nEmbd = llama_model_n_embd(model);

    auto ctxParams = llama_context_default_params();
    ctxParams.embeddings = true;
    ctxParams.pooling_type = LLAMA_POOLING_TYPE_MEAN;
    ctxParams.n_ctx = EMBEDDING_CONTEXT;
    ctxParams.n_batch = EMBEDDING_CONTEXT;
    ctxParams.n_ubatch = EMBEDDING_CONTEXT;

    ctx = llama_init_from_model(model, ctxParams);

    if (!ctx)
    {
        llama_model_free(model);
        model = nullptr;
        throw std::runtime_error("llama: failed to create embedding context");
    }

    loadedModelPath = modelPath;
    spdlog::info("[LlamaEmbeddingProvider] embedding model ready (n_embd={})", nEmbd);
}

std::vector<float> LlamaEmbeddingProvider::embedOne(const std::string &text) const
{
    auto tokens = common_tokenize(ctx, text, true, true);

    if (tokens.empty())
    {
        return {};
    }

    if (tokens.size() > EMBEDDING_CONTEXT)
    {
        tokens.resize(EMBEDDING_CONTEXT);
    }

    // a fresh sequence per text keeps the mean pooling scoped to this input alone
    llama_memory_clear(llama_get_memory(ctx), true);

    llama_batch batch = llama_batch_init(static_cast<int32_t>(tokens.size()), 0, 1);

    for (size_t i = 0; i < tokens.size(); ++i)
    {
        common_batch_add(batch, tokens[i], static_cast<llama_pos>(i), {0}, true);
    }

    auto rc = llama_model_has_encoder(model) ? llama_encode(ctx, batch) : llama_decode(ctx, batch);

    if (rc != 0)
    {
        llama_batch_free(batch);
        spdlog::error("[LlamaEmbeddingProvider] failed to run the model on the input");
        return {};
    }

    const float *embeddings = llama_get_embeddings_seq(ctx, 0);

    if (!embeddings)
    {
        llama_batch_free(batch);
        spdlog::error("[LlamaEmbeddingProvider] model returned no pooled embedding");
        return {};
    }

    std::vector<float> vector(embeddings, embeddings + nEmbd);
    llama_batch_free(batch);

    return vector;
}

std::vector<std::vector<float>> LlamaEmbeddingProvider::embed(const std::vector<std::string> &texts, const EmbeddingContext &context) const
{
    if (texts.empty())
    {
        return {};
    }

    auto modelPath = resolveModelPath(context);

    if (modelPath.empty())
    {
        spdlog::error("[LlamaEmbeddingProvider] no gguf model matches '{}'", context.model);
        return {};
    }

    std::lock_guard<std::mutex> lock(mutex);

    try
    {
        ensureModel(modelPath);
    }
    catch (const std::exception &e)
    {
        spdlog::error("[LlamaEmbeddingProvider] {}", e.what());
        return {};
    }

    std::vector<std::vector<float>> vectors;
    vectors.reserve(texts.size());

    for (const auto &text : texts)
    {
        auto vector = embedOne(text);

        if (vector.empty())
        {
            return {};
        }

        vectors.push_back(std::move(vector));
    }

    return vectors;
}

} // namespace embedding
} // namespace ionclaw

#endif // IONCLAW_HAS_LLAMA_CPP
