#ifdef IONCLAW_HAS_LLAMA_CPP

#include "ionclaw/provider/LlamaProvider.hpp"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>

#include "llama.h"

#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace provider
{

std::mutex LlamaProvider::backendMutex;
int LlamaProvider::backendRefCount = 0;

void LlamaProvider::acquireBackend()
{
    std::lock_guard<std::mutex> lock(backendMutex);
    if (backendRefCount == 0)
    {
        llama_backend_init();
    }
    ++backendRefCount;
}

void LlamaProvider::releaseBackend()
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

struct ContextDeleter
{
    void operator()(llama_context *ctx) const { llama_free(ctx); }
};

struct SamplerDeleter
{
    void operator()(llama_sampler *smpl) const { llama_sampler_free(smpl); }
};

using ContextPtr = std::unique_ptr<llama_context, ContextDeleter>;
using SamplerPtr = std::unique_ptr<llama_sampler, SamplerDeleter>;

LlamaProvider::LlamaProvider(const std::string &modelPath, const nlohmann::json &params)
    : modelPath(modelPath)
    , contextSize(params.is_object() ? std::max(1, params.value("context_size", 4096)) : 4096)
    , gpuLayers(params.is_object() ? params.value("gpu_layers", -1) : -1)
{
    acquireBackend();
    spdlog::info("[LlamaProvider] Initialized (model={}, context_size={}, gpu_layers={})",
                 modelPath, contextSize, gpuLayers);
}

LlamaProvider::~LlamaProvider()
{
    // signal abort then wait for any active inference to exit before releasing resources
    aborted.store(true, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(inferenceMutex);
    releaseModel();
    releaseBackend();
}

std::string LlamaProvider::name() const
{
    return "llama";
}

ChatCompletionResponse LlamaProvider::chat(const ChatCompletionRequest &request)
{
    std::lock_guard<std::mutex> lock(inferenceMutex);
    ensureModel();

    const auto prompt = buildPrompt(request);
    const auto params = resolveSamplerParams(request);

    ChatCompletionResponse response;
    response.content = generate(prompt, params, nullptr, nullptr);
    response.finishReason = "stop";

    const int promptTokens = static_cast<int>(prompt.size() / 4);
    const int completionTokens = static_cast<int>(response.content.size() / 4);
    response.usage = {
        {"prompt_tokens", promptTokens},
        {"completion_tokens", completionTokens},
        {"total_tokens", promptTokens + completionTokens},
    };

    return response;
}

void LlamaProvider::chatStream(const ChatCompletionRequest &request, StreamCallback callback)
{
    std::lock_guard<std::mutex> lock(inferenceMutex);
    ensureModel();

    const auto prompt = buildPrompt(request);
    const auto params = resolveSamplerParams(request);
    const auto content = generate(prompt, params, &callback, nullptr);

    const int promptTokens = static_cast<int>(prompt.size() / 4);
    const int completionTokens = static_cast<int>(content.size() / 4);

    StreamChunk usageChunk;
    usageChunk.type = "usage";
    usageChunk.usage = {
        {"prompt_tokens", promptTokens},
        {"completion_tokens", completionTokens},
        {"total_tokens", promptTokens + completionTokens},
    };
    callback(usageChunk);

    StreamChunk doneChunk;
    doneChunk.type = "done";
    doneChunk.finishReason = "stop";
    callback(doneChunk);
}

void LlamaProvider::ensureModel()
{
    if (model)
    {
        return;
    }

    spdlog::info("[LlamaProvider] Loading model: {}", modelPath);

    auto modelParams = llama_model_default_params();
    modelParams.n_gpu_layers = gpuLayers;

    model = llama_model_load_from_file(modelPath.c_str(), modelParams);

    if (!model)
    {
        throw std::runtime_error("llama: failed to load model from '" + modelPath + "'");
    }

    spdlog::info("[LlamaProvider] Model loaded successfully");
}

void LlamaProvider::releaseModel()
{
    if (model)
    {
        llama_model_free(model);
        model = nullptr;
    }
}

std::string LlamaProvider::buildPrompt(const ChatCompletionRequest &request) const
{
    const auto *tmpl = llama_model_chat_template(model, nullptr);

    if (tmpl)
    {
        std::vector<llama_chat_message> messages;
        messages.reserve(request.messages.size());
        for (const auto &msg : request.messages)
        {
            messages.push_back({msg.role.c_str(), msg.content.c_str()});
        }

        std::vector<char> buf(request.messages.size() * 512);
        int32_t result = llama_chat_apply_template(
            tmpl, messages.data(), messages.size(), true, buf.data(), static_cast<int32_t>(buf.size()));

        if (result < 0)
        {
            throw std::runtime_error("llama: failed to apply chat template");
        }

        // resize and retry if the initial buffer estimate was too small
        if (static_cast<size_t>(result) > buf.size())
        {
            buf.resize(static_cast<size_t>(result) + 1);
            result = llama_chat_apply_template(
                tmpl, messages.data(), messages.size(), true, buf.data(), static_cast<int32_t>(buf.size()));

            if (result < 0)
            {
                throw std::runtime_error("llama: failed to apply chat template");
            }
        }

        return std::string(buf.data(), static_cast<size_t>(result));
    }

    // no built-in template: fall back to ChatML format
    std::string prompt;
    prompt.reserve(request.messages.size() * 256);
    for (const auto &msg : request.messages)
    {
        prompt += "<|im_start|>";
        prompt += msg.role;
        prompt += '\n';
        prompt += msg.content;
        prompt += "<|im_end|>\n";
    }
    prompt += "<|im_start|>assistant\n";
    return prompt;
}

LlamaProvider::SamplerParams LlamaProvider::resolveSamplerParams(const ChatCompletionRequest &request) const
{
    SamplerParams p;
    p.maxTokens = request.maxTokens;
    p.temperature = request.temperature;

    if (!request.modelParams.is_object())
    {
        return p;
    }

    const auto &mp = request.modelParams;
    p.maxTokens = mp.value("max_tokens", p.maxTokens);
    p.temperature = mp.value("temperature", p.temperature);
    p.topP = mp.value("top_p", p.topP);
    p.topK = mp.value("top_k", p.topK);
    p.repeatPenalty = mp.value("repeat_penalty", p.repeatPenalty);

    return p;
}

// returns the number of trailing bytes that form an incomplete UTF-8 sequence
size_t LlamaProvider::incompleteUtf8Tail(const std::string &s)
{
    const size_t n = s.size();
    if (n == 0)
    {
        return 0;
    }

    size_t tail = 0;
    while (tail < n && tail < 4)
    {
        const unsigned char c = static_cast<unsigned char>(s[n - 1 - tail]);
        if ((c & 0xC0) != 0x80)
        {
            size_t seqLen = 1;
            if ((c & 0x80) == 0x00)
                seqLen = 1;
            else if ((c & 0xE0) == 0xC0)
                seqLen = 2;
            else if ((c & 0xF0) == 0xE0)
                seqLen = 3;
            else if ((c & 0xF8) == 0xF0)
                seqLen = 4;
            else
                return 0;

            return (tail + 1 == seqLen) ? 0 : tail + 1;
        }
        ++tail;
    }
    return tail;
}

std::string LlamaProvider::generate(const std::string &prompt, const SamplerParams &params,
                                    StreamCallback *callback, std::atomic<bool> *cancelled)
{
    auto ctxParams = llama_context_default_params();
    ctxParams.n_ctx = static_cast<uint32_t>(contextSize);

    ContextPtr ctx(llama_init_from_model(model, ctxParams));
    if (!ctx)
    {
        throw std::runtime_error("llama: failed to create context");
    }

    // abort callback allows llama_decode to be interrupted mid-execution
    struct AbortState
    {
        std::atomic<bool> *aborted;
        std::atomic<bool> *cancelled;
    };
    AbortState abortState{&aborted, cancelled};

    llama_set_abort_callback(ctx.get(), [](void *data) -> bool
                             {
            const auto *s = static_cast<const AbortState *>(data);
            return s->aborted->load(std::memory_order_relaxed) ||
                   (s->cancelled && s->cancelled->load(std::memory_order_relaxed)); }, &abortState);

    const auto *vocab = llama_model_get_vocab(model);

    // negative return from llama_tokenize means buffer was too small; abs value is the required size
    std::vector<llama_token> tokens(static_cast<size_t>(contextSize));
    int32_t nTokens = llama_tokenize(vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()),
                                     tokens.data(), static_cast<int32_t>(tokens.size()), true, true);

    if (nTokens < 0)
    {
        tokens.resize(static_cast<size_t>(-nTokens));
        nTokens = llama_tokenize(vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()),
                                 tokens.data(), static_cast<int32_t>(tokens.size()), true, true);
        if (nTokens < 0)
        {
            throw std::runtime_error("llama: tokenization failed");
        }
    }

    if (nTokens >= static_cast<int32_t>(ctxParams.n_ctx))
    {
        throw std::runtime_error("llama: context_overflow - prompt too long (" +
                                 std::to_string(nTokens) + " tokens, limit " +
                                 std::to_string(ctxParams.n_ctx) + ")");
    }

    tokens.resize(static_cast<size_t>(nTokens));

    SamplerPtr smpl(llama_sampler_chain_init(llama_sampler_chain_default_params()));
    if (!smpl)
    {
        throw std::runtime_error("llama: failed to create sampler chain");
    }

    if (params.temperature <= 0.0)
    {
        llama_sampler_chain_add(smpl.get(), llama_sampler_init_greedy());
    }
    else
    {
        if (params.repeatPenalty != 1.0)
        {
            llama_sampler_chain_add(smpl.get(), llama_sampler_init_penalties(
                                                    0,
                                                    static_cast<float>(params.repeatPenalty),
                                                    0.0f,
                                                    0.0f));
        }
        if (params.topK > 0)
        {
            llama_sampler_chain_add(smpl.get(), llama_sampler_init_top_k(params.topK));
        }
        llama_sampler_chain_add(smpl.get(), llama_sampler_init_top_p(static_cast<float>(params.topP), 1));
        llama_sampler_chain_add(smpl.get(), llama_sampler_init_temp(static_cast<float>(params.temperature)));
        llama_sampler_chain_add(smpl.get(), llama_sampler_init_dist(0));
    }

    // process the prompt in chunks to respect n_batch limits
    const int32_t nBatch = static_cast<int32_t>(ctxParams.n_batch);
    for (int32_t i = 0; i < nTokens; i += nBatch)
    {
        if (aborted.load(std::memory_order_relaxed))
        {
            return {};
        }

        const int32_t chunk = std::min(nBatch, nTokens - i);
        llama_batch batch = llama_batch_get_one(tokens.data() + i, chunk);

        if (llama_decode(ctx.get(), batch) != 0)
        {
            if (aborted.load(std::memory_order_relaxed))
            {
                return {};
            }
            spdlog::error("[LlamaProvider] Prefill failed at token offset {}", i);
            throw std::runtime_error("llama: prompt decoding failed");
        }
    }

    std::string output;
    const int32_t remainingCtx = static_cast<int32_t>(ctxParams.n_ctx) - nTokens;
    const int32_t genLimit = std::max(int32_t{0}, std::min(params.maxTokens, remainingCtx));
    output.reserve(static_cast<size_t>(genLimit) * 4);

    std::string utf8Pending;

    for (int32_t i = 0; i < genLimit; ++i)
    {
        if (aborted.load(std::memory_order_relaxed) ||
            (cancelled && cancelled->load(std::memory_order_relaxed)))
        {
            break;
        }

        llama_token newToken = llama_sampler_sample(smpl.get(), ctx.get(), -1);

        if (llama_vocab_is_eog(vocab, newToken))
        {
            break;
        }

        char buf[256];
        const int32_t len = llama_token_to_piece(vocab, newToken, buf, static_cast<int32_t>(sizeof(buf)), 0, true);

        if (len < 0)
        {
            spdlog::warn("[LlamaProvider] Token piece too large for buffer, skipping token {}", newToken);
        }
        else if (len > 0)
        {
            const std::string piece(buf, static_cast<size_t>(len));
            output += piece;

            if (callback)
            {
                utf8Pending += piece;
                const size_t incomplete = incompleteUtf8Tail(utf8Pending);
                const size_t safeLen = utf8Pending.size() - incomplete;
                if (safeLen > 0)
                {
                    StreamChunk chunk;
                    chunk.type = "content";
                    chunk.content = utf8Pending.substr(0, safeLen);
                    utf8Pending.erase(0, safeLen);
                    (*callback)(chunk);
                }
            }
        }

        llama_batch nextBatch = llama_batch_get_one(&newToken, 1);
        if (llama_decode(ctx.get(), nextBatch) != 0)
        {
            if (!aborted.load(std::memory_order_relaxed))
            {
                spdlog::error("[LlamaProvider] Decode failed at generation step {}", i);
            }
            break;
        }
    }

    // flush any remaining bytes held for utf-8 boundary alignment
    if (callback && !utf8Pending.empty())
    {
        StreamChunk chunk;
        chunk.type = "content";
        chunk.content = utf8Pending;
        (*callback)(chunk);
    }

    return output;
}

} // namespace provider
} // namespace ionclaw

#endif // IONCLAW_HAS_LLAMA_CPP
