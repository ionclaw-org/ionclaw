#ifdef IONCLAW_HAS_LLAMA_CPP

#include "ionclaw/provider/LlamaProvider.hpp"

#include "ionclaw/provider/ProviderHelper.hpp"

#include <algorithm>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "llama.h"

#include "chat.h"
#include "common.h"
#include "sampling.h"

#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace provider
{

std::mutex LlamaProvider::backendMutex;
int LlamaProvider::backendRefCount = 0;

namespace
{

struct CommonSamplerDeleter
{
    void operator()(common_sampler *smpl) const { common_sampler_free(smpl); }
};

using CommonSamplerPtr = std::unique_ptr<common_sampler, CommonSamplerDeleter>;

struct SamplerParams
{
    int32_t maxTokens = 4096;
    int32_t topK = 40;
    int32_t repeatLastN = 64;
    double temperature = 0.7;
    double topP = 0.95;
    double repeatPenalty = 1.1;
};

// returns the number of trailing bytes that form an incomplete utf-8 sequence
size_t incompleteUtf8Tail(const std::string &text)
{
    const size_t n = text.size();

    if (n == 0)
    {
        return 0;
    }

    size_t tail = 0;

    while (tail < n && tail < 4)
    {
        const unsigned char c = static_cast<unsigned char>(text[n - 1 - tail]);

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

bool shouldStop(const std::atomic<bool> &aborted, const CancelPredicate &isCancelled)
{
    return aborted.load(std::memory_order_relaxed) || (isCancelled && isCancelled());
}

SamplerParams resolveSamplerParams(const ChatCompletionRequest &request)
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
    p.topK = mp.value("top_k", p.topK);
    p.repeatLastN = mp.value("repeat_last_n", p.repeatLastN);
    p.temperature = mp.value("temperature", p.temperature);
    p.topP = mp.value("top_p", p.topP);
    p.repeatPenalty = mp.value("repeat_penalty", p.repeatPenalty);

    return p;
}

// maps the request history and tool definitions into the chat-template input format
common_chat_templates_inputs buildChatInputs(const ChatCompletionRequest &request)
{
    common_chat_templates_inputs inputs;
    inputs.add_generation_prompt = true;
    inputs.use_jinja = true;
    inputs.parallel_tool_calls = true;

    inputs.messages.reserve(request.messages.size());

    for (const auto &msg : request.messages)
    {
        common_chat_msg out;
        out.role = msg.role;
        out.content = msg.content;
        out.reasoning_content = msg.reasoningContent;
        out.tool_call_id = msg.toolCallId;

        if (msg.role == "tool")
        {
            out.tool_name = msg.name;
        }

        for (const auto &tc : msg.toolCalls)
        {
            common_chat_tool_call call;
            call.id = tc.id;
            call.name = tc.name;
            call.arguments = tc.arguments.is_string() ? tc.arguments.get<std::string>() : tc.arguments.dump();
            out.tool_calls.push_back(std::move(call));
        }

        inputs.messages.push_back(std::move(out));
    }

    for (const auto &tool : request.tools)
    {
        const auto &fn = tool.contains("function") ? tool.at("function") : tool;

        common_chat_tool out;
        out.name = fn.value("name", std::string());
        out.description = fn.value("description", std::string());
        out.parameters = fn.contains("parameters") ? fn.at("parameters").dump() : std::string("{}");

        inputs.tools.push_back(std::move(out));
    }

    return inputs;
}

// translates our sampling settings and the template's tool-call grammar into a common sampler config
common_params_sampling buildSamplingParams(const common_chat_params &chatParams, const SamplerParams &params, const llama_vocab *vocab)
{
    common_params_sampling sp;
    sp.seed = 0;
    sp.temp = static_cast<float>(params.temperature);
    sp.top_k = params.topK;
    sp.top_p = static_cast<float>(params.topP);
    sp.penalty_repeat = static_cast<float>(params.repeatPenalty);
    sp.penalty_last_n = params.repeatLastN;

    if (chatParams.grammar.empty())
    {
        return sp;
    }

    sp.grammar = common_grammar(COMMON_GRAMMAR_TYPE_TOOL_CALLS, chatParams.grammar);
    sp.grammar_lazy = chatParams.grammar_lazy;

    for (const auto &token : chatParams.preserved_tokens)
    {
        const auto ids = common_tokenize(vocab, token, false, true);

        if (ids.size() == 1)
        {
            sp.preserved_tokens.insert(ids[0]);
        }
    }

    for (const auto &trigger : chatParams.grammar_triggers)
    {
        if (trigger.type != COMMON_GRAMMAR_TRIGGER_TYPE_WORD)
        {
            sp.grammar_triggers.push_back(trigger);
            continue;
        }

        // a single-token trigger word is matched far more cheaply as a token trigger
        const auto ids = common_tokenize(vocab, trigger.value, false, true);

        if (ids.size() != 1)
        {
            sp.grammar_triggers.push_back(trigger);
            continue;
        }

        common_grammar_trigger tokenTrigger;
        tokenTrigger.type = COMMON_GRAMMAR_TRIGGER_TYPE_TOKEN;
        tokenTrigger.value = trigger.value;
        tokenTrigger.token = ids[0];
        sp.grammar_triggers.push_back(std::move(tokenTrigger));
    }

    return sp;
}

std::vector<llama_token> tokenizePrompt(const llama_vocab *vocab, const std::string &prompt, uint32_t nCtx)
{
    // a negative return means the buffer was too small and its absolute value is the required size
    std::vector<llama_token> tokens(nCtx);
    int32_t count = llama_tokenize(vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()), tokens.data(), static_cast<int32_t>(tokens.size()), true, true);

    if (count < 0)
    {
        tokens.resize(static_cast<size_t>(-count));
        count = llama_tokenize(vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()), tokens.data(), static_cast<int32_t>(tokens.size()), true, true);

        if (count < 0)
        {
            throw std::runtime_error("llama: tokenization failed");
        }
    }

    if (count >= static_cast<int32_t>(nCtx))
    {
        throw std::runtime_error("llama: context_overflow - prompt too long (" + std::to_string(count) + " tokens, limit " + std::to_string(nCtx) + ")");
    }

    tokens.resize(static_cast<size_t>(count));
    return tokens;
}

// feeds the prompt in n_batch chunks, returning false when the user cancels mid-prefill
bool prefill(llama_context *ctx, std::vector<llama_token> &tokens, const std::atomic<bool> &aborted, const CancelPredicate &isCancelled)
{
    const int32_t nTokens = static_cast<int32_t>(tokens.size());
    const int32_t nBatch = static_cast<int32_t>(llama_n_batch(ctx));

    for (int32_t i = 0; i < nTokens; i += nBatch)
    {
        if (shouldStop(aborted, isCancelled))
        {
            return false;
        }

        const int32_t chunk = std::min(nBatch, nTokens - i);
        llama_batch batch = llama_batch_get_one(tokens.data() + i, chunk);

        if (llama_decode(ctx, batch) == 0)
        {
            continue;
        }

        // a non-zero result during a cancel is the abort callback firing, not a real failure
        if (shouldStop(aborted, isCancelled))
        {
            return false;
        }

        spdlog::error("[LlamaProvider] Prefill failed at token offset {}", i);
        throw std::runtime_error("llama: prompt decoding failed");
    }

    return true;
}

// streams the content and reasoning that appeared since the previous parse, holding back partial utf-8
void emitContentDeltas(const std::string &generated, common_chat_msg &previous, const common_chat_parser_params &parserParams, std::string &utf8Pending, const StreamCallback &callback)
{
    common_chat_msg current;

    // the model output may not parse cleanly until more tokens arrive, so skip this tick on failure
    try
    {
        current = common_chat_parse(generated, true, parserParams);
    }
    catch (const std::exception &)
    {
        return;
    }

    for (const auto &diff : common_chat_msg_diff::compute_diffs(previous, current))
    {
        if (!diff.reasoning_content_delta.empty())
        {
            StreamChunk chunk;
            chunk.type = "thinking";
            chunk.content = diff.reasoning_content_delta;
            callback(chunk);
        }

        if (diff.content_delta.empty())
        {
            continue;
        }

        utf8Pending += diff.content_delta;
        const size_t incomplete = incompleteUtf8Tail(utf8Pending);
        const size_t safeLen = utf8Pending.size() - incomplete;

        if (safeLen > 0)
        {
            StreamChunk chunk;
            chunk.type = "content";
            chunk.content = utf8Pending.substr(0, safeLen);
            utf8Pending.erase(0, safeLen);
            callback(chunk);
        }
    }

    previous = std::move(current);
}

// trims a trailing stop string required by the template and reports whether one matched
bool stripAdditionalStop(std::string &generated, const std::vector<std::string> &stops)
{
    for (const auto &stop : stops)
    {
        if (stop.empty() || generated.size() < stop.size())
        {
            continue;
        }

        if (generated.compare(generated.size() - stop.size(), stop.size(), stop) == 0)
        {
            generated.erase(generated.size() - stop.size());
            return true;
        }
    }

    return false;
}

} // namespace

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

LlamaProvider::LlamaProvider(const std::string &modelPath, const nlohmann::json &params)
    : modelPath(modelPath)
    , contextSize(params.is_object() ? std::max(1, params.value("context_size", 4096)) : 4096)
    , gpuLayers(params.is_object() ? params.value("gpu_layers", -1) : -1)
{
    acquireBackend();

    spdlog::info("[LlamaProvider] Initialized (model={}, context_size={}, gpu_layers={})", modelPath, contextSize, gpuLayers);
}

LlamaProvider::~LlamaProvider()
{
    // signal abort, then wait for any active inference to finish before freeing resources
    aborted.store(true, std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(inferenceMutex);
    releaseTemplates();
    releaseContext();
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

    const auto gen = generate(request, nullptr, {});

    ChatCompletionResponse response;
    response.content = gen.content;
    response.reasoningContent = gen.reasoningContent;
    response.toolCalls = gen.toolCalls;
    response.finishReason = gen.finishReason;
    response.usage = {
        {"prompt_tokens", gen.promptTokens},
        {"completion_tokens", gen.completionTokens},
        {"total_tokens", gen.promptTokens + gen.completionTokens},
    };

    return response;
}

void LlamaProvider::chatStream(const ChatCompletionRequest &request, StreamCallback callback, const CancelPredicate &isCancelled)
{
    std::lock_guard<std::mutex> lock(inferenceMutex);

    const auto gen = generate(request, &callback, isCancelled);

    // content and reasoning were streamed during generation, so only the finalized tool calls remain
    for (const auto &toolCall : gen.toolCalls)
    {
        StreamChunk chunk;
        chunk.type = "tool_call";
        chunk.toolCall = toolCall;
        callback(chunk);
    }

    StreamChunk usageChunk;
    usageChunk.type = "usage";
    usageChunk.usage = {
        {"prompt_tokens", gen.promptTokens},
        {"completion_tokens", gen.completionTokens},
        {"total_tokens", gen.promptTokens + gen.completionTokens},
    };
    callback(usageChunk);

    StreamChunk doneChunk;
    doneChunk.type = "done";
    doneChunk.finishReason = gen.finishReason;
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

void LlamaProvider::ensureContext()
{
    if (ctx)
    {
        return;
    }

    auto ctxParams = llama_context_default_params();
    ctxParams.n_ctx = static_cast<uint32_t>(contextSize);

    ctx = llama_init_from_model(model, ctxParams);

    if (!ctx)
    {
        throw std::runtime_error("llama: failed to create context");
    }

    spdlog::info("[LlamaProvider] Context ready (n_ctx={})", llama_n_ctx(ctx));
}

void LlamaProvider::ensureTemplates()
{
    if (templates)
    {
        return;
    }

    auto initialized = common_chat_templates_init(model, "");

    if (!initialized)
    {
        throw std::runtime_error("llama: failed to initialize chat templates");
    }

    templates = initialized.release();
}

void LlamaProvider::releaseTemplates()
{
    if (templates)
    {
        common_chat_templates_free(templates);
        templates = nullptr;
    }
}

void LlamaProvider::releaseContext()
{
    if (ctx)
    {
        llama_free(ctx);
        ctx = nullptr;
    }
}

void LlamaProvider::releaseModel()
{
    if (model)
    {
        llama_model_free(model);
        model = nullptr;
    }
}

LlamaProvider::GenerationResult LlamaProvider::generate(const ChatCompletionRequest &request, const StreamCallback *callback, const CancelPredicate &isCancelled)
{
    ensureModel();
    ensureContext();
    ensureTemplates();

    // the context is reused across turns, so drop the previous turn's tokens before prefill
    llama_memory_clear(llama_get_memory(ctx), true);

    // the model's own chat template produces the prompt and, when tools are present, a tool-call grammar
    const auto inputs = buildChatInputs(request);
    const auto chatParams = common_chat_templates_apply(templates, inputs);

    // the abort callback lets llama_decode be interrupted mid-execution
    struct AbortState
    {
        std::atomic<bool> *aborted;
        const CancelPredicate *cancelled;
    };
    AbortState abortState{&aborted, &isCancelled};

    llama_set_abort_callback(ctx, [](void *data) -> bool {
        const auto *s = static_cast<const AbortState *>(data);
        return s->aborted->load(std::memory_order_relaxed) || (*s->cancelled && (*s->cancelled)());
    }, &abortState);

    // abortState lives on this stack, so detach the callback before returning to avoid a dangling pointer
    struct AbortGuard
    {
        llama_context *ctx;
        ~AbortGuard() { llama_set_abort_callback(ctx, nullptr, nullptr); }
    } abortGuard{ctx};

    const auto *vocab = llama_model_get_vocab(model);
    const uint32_t nCtx = llama_n_ctx(ctx);

    auto tokens = tokenizePrompt(vocab, chatParams.prompt, nCtx);
    const int32_t promptTokens = static_cast<int32_t>(tokens.size());

    if (!prefill(ctx, tokens, aborted, isCancelled))
    {
        return {};
    }

    const auto samplerParams = resolveSamplerParams(request);
    auto sp = buildSamplingParams(chatParams, samplerParams, vocab);

    CommonSamplerPtr smpl(common_sampler_init(model, sp));

    if (!smpl)
    {
        throw std::runtime_error("llama: failed to create sampler");
    }

    // the parser maps raw output into content, reasoning and tool calls for the chosen format
    common_chat_parser_params parserParams(chatParams);

    if (!chatParams.parser.empty())
    {
        parserParams.parser.load(chatParams.parser);
    }

    const int32_t genLimit = std::max(int32_t{0}, std::min(samplerParams.maxTokens, static_cast<int32_t>(nCtx) - promptTokens));

    std::string generated;
    generated.reserve(static_cast<size_t>(genLimit) * 4);

    common_chat_msg streamedMsg;
    std::string utf8Pending;
    int32_t generatedTokens = 0;
    bool completed = false;

    for (int32_t i = 0; i < genLimit; ++i)
    {
        if (shouldStop(aborted, isCancelled))
        {
            break;
        }

        llama_token newToken = common_sampler_sample(smpl.get(), ctx, -1);
        common_sampler_accept(smpl.get(), newToken, true);

        if (llama_vocab_is_eog(vocab, newToken))
        {
            completed = true;
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
            generated.append(buf, static_cast<size_t>(len));
            ++generatedTokens;

            // honor any stop strings the template requires beyond the end-of-generation token
            if (stripAdditionalStop(generated, chatParams.additional_stops))
            {
                completed = true;
                break;
            }

            if (callback)
            {
                emitContentDeltas(generated, streamedMsg, parserParams, utf8Pending, *callback);
            }
        }

        llama_batch nextBatch = llama_batch_get_one(&newToken, 1);

        if (llama_decode(ctx, nextBatch) != 0)
        {
            // suppress the error log when the failure is just the abort callback firing on cancel
            if (!shouldStop(aborted, isCancelled))
            {
                spdlog::error("[LlamaProvider] Decode failed at generation step {}", i);
            }

            break;
        }
    }

    // flush any bytes held back for utf-8 boundary alignment
    if (callback && !utf8Pending.empty())
    {
        StreamChunk chunk;
        chunk.type = "content";
        chunk.content = utf8Pending;
        (*callback)(chunk);
    }

    GenerationResult result;
    result.promptTokens = promptTokens;
    result.completionTokens = generatedTokens;

    if (generated.empty())
    {
        result.finishReason = "stop";
        return result;
    }

    common_chat_msg parsed;

    // an interrupted generation only parses partially, so fall back when the strict parse fails
    try
    {
        parsed = common_chat_parse(generated, !completed, parserParams);
    }
    catch (const std::exception &)
    {
        parsed = common_chat_parse(generated, true, parserParams);
    }

    result.content = parsed.content;
    result.reasoningContent = parsed.reasoning_content;

    for (const auto &call : parsed.tool_calls)
    {
        ToolCall toolCall;
        toolCall.id = ProviderHelper::sanitizeToolCallId(call.id);
        toolCall.name = ProviderHelper::sanitizeToolCallName(call.name);
        toolCall.arguments = ProviderHelper::repairJsonArgs(call.arguments);
        result.toolCalls.push_back(std::move(toolCall));
    }

    result.finishReason = result.toolCalls.empty() ? "stop" : "tool_calls";

    return result;
}

} // namespace provider
} // namespace ionclaw

#endif // IONCLAW_HAS_LLAMA_CPP
