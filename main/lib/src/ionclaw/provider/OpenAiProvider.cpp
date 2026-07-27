#include "ionclaw/provider/OpenAiProvider.hpp"

#include <algorithm>
#include <regex>
#include <stdexcept>

#include "ionclaw/provider/ModelCapabilities.hpp"
#include "ionclaw/provider/ProviderHelper.hpp"
#include "ionclaw/util/HttpClient.hpp"
#include "ionclaw/util/StringHelper.hpp"
#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace provider
{

void OpenAiProvider::sanitizeMessages(nlohmann::json &messages)
{
    for (auto &msg : messages)
    {
        auto role = msg.value("role", "");

        // empty content for user messages
        if (role == "user" && msg.contains("content"))
        {
            if (msg["content"].is_string() && msg["content"].get<std::string>().empty())
            {
                msg["content"] = "(empty)";
            }
        }
    }
}

nlohmann::json OpenAiProvider::validateTranscript(const nlohmann::json &messages)
{
    nlohmann::json validated = nlohmann::json::array();

    for (const auto &msg : messages)
    {
        auto role = msg.value("role", "");

        // system messages pass through unchanged
        if (role == "system")
        {
            validated.push_back(msg);
            continue;
        }

        // tool messages must follow an assistant message with tool_calls
        if (role == "tool")
        {
            // find last non-tool message
            nlohmann::json *lastNonTool = nullptr;

            for (auto it = validated.rbegin(); it != validated.rend(); ++it)
            {
                if ((*it).value("role", "") != "tool")
                {
                    lastNonTool = &(*it);
                    break;
                }
            }

            if (lastNonTool && (*lastNonTool).value("role", "") == "assistant" && (*lastNonTool).contains("tool_calls") && (*lastNonTool)["tool_calls"].is_array())
            {
                validated.push_back(msg);
            }
            else
            {
                spdlog::debug("[OpenAiProvider] Dropping orphaned tool result (tool_call_id={})", msg.value("tool_call_id", "?"));
            }

            continue;
        }

        // merge consecutive user messages
        if (role == "user")
        {
            if (!validated.empty() && validated.back().value("role", "") == "user")
            {
                auto &prev = validated.back();

                if (prev.contains("content") && prev["content"].is_string() && msg.contains("content") && msg["content"].is_string())
                {
                    prev["content"] = prev["content"].get<std::string>() + "\n\n" + msg["content"].get<std::string>();
                }
                else
                {
                    // insert placeholder assistant message to maintain role alternation
                    validated.push_back({{"role", "assistant"}, {"content", ""}});
                    validated.push_back(msg);
                }
            }
            else
            {
                validated.push_back(msg);
            }

            continue;
        }

        // merge consecutive assistant messages (when neither has tool_calls)
        if (role == "assistant")
        {
            if (!validated.empty() && validated.back().value("role", "") == "assistant" && !validated.back().contains("tool_calls") && !msg.contains("tool_calls"))
            {
                auto &prev = validated.back();

                // only merge when both have string content (skip array content blocks)
                bool prevIsString = prev.contains("content") && prev["content"].is_string();
                bool curIsString = msg.contains("content") && msg["content"].is_string();

                if (prevIsString && curIsString)
                {
                    auto prevContent = prev["content"].get<std::string>();
                    auto curContent = msg["content"].get<std::string>();

                    if (prevContent.empty())
                    {
                        prev["content"] = curContent;
                    }
                    else if (!curContent.empty())
                    {
                        prev["content"] = prevContent + "\n\n" + curContent;
                    }
                }
                else
                {
                    // incompatible content types, insert separator to maintain alternation
                    validated.push_back({{"role", "user"}, {"content", "[continued]"}});
                    validated.push_back(msg);
                }
            }
            else if (!validated.empty() && validated.back().value("role", "") == "assistant")
            {
                // insert placeholder user message to maintain role alternation
                validated.push_back({{"role", "user"}, {"content", "[continued]"}});
                validated.push_back(msg);
            }
            else
            {
                validated.push_back(msg);
            }

            continue;
        }

        validated.push_back(msg);
    }

    return validated;
}

OpenAiProvider::OpenAiProvider(const std::string &apiKey, const std::string &baseUrl, int timeout, const std::map<std::string, std::string> &extraHeaders)
    : apiKey(apiKey)
    , baseUrl(baseUrl)
    , timeout(timeout)
    , extraHeaders(extraHeaders)
{
}

std::string OpenAiProvider::name() const
{
    return "openai";
}

bool OpenAiProvider::isReasoningModel(const std::string &model)
{
    auto m = model;
    ionclaw::util::StringHelper::toLowerInPlace(m);

    // o1/o3/o4 families (o1, o1-mini, o3-mini, o4-mini) and the gpt-5 reasoning family
    return m.starts_with("o1") || m.starts_with("o3") || m.starts_with("o4") || m.starts_with("gpt-5");
}

std::string OpenAiProvider::stringField(const nlohmann::json &obj, const std::string &key, const std::string &fallback)
{
    auto it = obj.find(key);

    if (it == obj.end() || !it->is_string())
    {
        return fallback;
    }

    return it->get<std::string>();
}

nlohmann::json OpenAiProvider::buildRequestBody(const ChatCompletionRequest &request) const
{
    // set base request fields
    nlohmann::json body;
    auto modelName = ProviderHelper::stripProviderPrefix(request.model);
    body["model"] = modelName;

    // reasoning models use max_completion_tokens and reject a non-default temperature, so they omit both max_tokens and temperature
    bool reasoningModel = isReasoningModel(modelName);

    if (reasoningModel)
    {
        body["max_completion_tokens"] = request.maxTokens;
    }
    else
    {
        body["temperature"] = request.temperature;
        body["max_tokens"] = request.maxTokens;
    }

    // apply model params from config
    std::string thinkingLevel;

    if (request.modelParams.is_object())
    {
        for (auto &[key, value] : request.modelParams.items())
        {
            if (key == "context_window")
            {
                continue; // skip internal keys
            }

            if (key == "thinking")
            {
                thinkingLevel = value.is_string() ? value.get<std::string>() : "";
                continue; // handled separately below
            }

            // reasoning models reject these, so they are not passed through from config
            if (reasoningModel && (key == "temperature" || key == "max_tokens"))
            {
                continue;
            }

            body[key] = value;
        }
    }

    // apply thinking/reasoning budget (for o1, o3, Claude with extended thinking via OpenAI-compat)
    // gated on the capability table so we never send a reasoning field to a model known not to support it (permissive when unknown)
    if (!thinkingLevel.empty() && thinkingLevel != "off" && ModelCapabilities::supportsReasoning(request.model))
    {
        // openrouter: inject reasoning.effort instead of thinking block
        bool isOpenRouter = baseUrl.find("openrouter") != std::string::npos;

        if (isOpenRouter)
        {
            // skip reasoning injection for openrouter/auto and x-ai/grok models
            auto modelLower = request.model;
            ionclaw::util::StringHelper::toLowerInPlace(modelLower);
            bool skipReasoning = modelLower.find("openrouter/auto") != std::string::npos || modelLower.find("x-ai/grok") != std::string::npos;

            if (!skipReasoning)
            {
                // map the thinking level to openrouter reasoning.effort, treating adaptive as medium
                std::string effort = thinkingLevel;

                if (thinkingLevel == "adaptive")
                {
                    effort = "medium";
                }

                // preserve existing reasoning.max_tokens if present
                if (body.contains("reasoning") && body["reasoning"].is_object())
                {
                    body["reasoning"]["effort"] = effort;
                }
                else
                {
                    body["reasoning"] = {{"effort", effort}};
                }

                // remove the flat reasoning_effort field, which conflicts with the nested reasoning.effort format
                body.erase("reasoning_effort");
            }
        }
        else
        {
            // openai-compatible providers use the standard reasoning_effort field, not anthropic's thinking block which they reject
            std::string effort = thinkingLevel == "adaptive" ? "medium" : thinkingLevel;

            if (effort == "low" || effort == "medium" || effort == "high")
            {
                body["reasoning_effort"] = effort;
                body["temperature"] = 1; // openai reasoning models only accept the default temperature
            }
        }
    }

    // convert messages to openai format
    auto messages = nlohmann::json::array();

    for (const auto &msg : request.messages)
    {
        nlohmann::json m;
        m["role"] = msg.role;

        // openai rejects images inside a tool message, so any are carried in a user message pushed right after
        nlohmann::json pendingImageMessage = nullptr;

        // convert assistant messages with tool calls
        if (msg.role == "assistant" && !msg.toolCalls.empty())
        {
            if (!msg.content.empty())
            {
                m["content"] = msg.content;
            }
            else
            {
                m["content"] = nullptr;
            }

            auto toolCallsJson = nlohmann::json::array();

            for (const auto &tc : msg.toolCalls)
            {
                nlohmann::json tcJson;
                tcJson["id"] = tc.id;
                tcJson["type"] = "function";
                tcJson["function"]["name"] = tc.name;
                tcJson["function"]["arguments"] = tc.arguments.dump();
                toolCallsJson.push_back(tcJson);
            }

            m["tool_calls"] = toolCallsJson;
        }
        // convert tool result messages
        else if (msg.role == "tool")
        {
            m["tool_call_id"] = msg.toolCallId;

            if (!msg.name.empty())
            {
                m["name"] = msg.name;
            }

            // the tool message keeps only text; images are moved to a following user message so openai accepts the request
            if (msg.contentBlocks.is_array() && !msg.contentBlocks.empty())
            {
                std::string toolText;
                auto imageBlocks = nlohmann::json::array();

                for (const auto &block : msg.contentBlocks)
                {
                    auto blockType = block.value("type", "");

                    if (blockType == "image")
                    {
                        auto mediaType = block.value("media_type", "image/png");
                        auto data = block.value("data", "");
                        nlohmann::json imageBlock;
                        imageBlock["type"] = "image_url";
                        imageBlock["image_url"]["url"] = "data:" + mediaType + ";base64," + data;
                        imageBlocks.push_back(imageBlock);
                    }
                    else if (blockType == "text")
                    {
                        if (!toolText.empty())
                        {
                            toolText += "\n";
                        }

                        toolText += block.value("text", "");
                    }
                }

                if (!imageBlocks.empty())
                {
                    auto suffix = std::string("[image result included in the following message]");
                    toolText = toolText.empty() ? suffix : toolText + "\n" + suffix;

                    pendingImageMessage = {{"role", "user"}, {"content", imageBlocks}};
                }

                m["content"] = toolText;
            }
            else
            {
                m["content"] = msg.content;
            }
        }
        // handle multimodal content blocks (images, audio)
        else if (msg.role == "user" && msg.contentBlocks.is_array() && !msg.contentBlocks.empty())
        {
            auto openaiBlocks = nlohmann::json::array();

            for (const auto &block : msg.contentBlocks)
            {
                auto blockType = block.value("type", "");

                if (blockType == "text")
                {
                    openaiBlocks.push_back(block);
                }
                else if (blockType == "image")
                {
                    auto url = block.value("url", "");

                    if (!url.empty())
                    {
                        nlohmann::json imageBlock;
                        imageBlock["type"] = "image_url";
                        imageBlock["image_url"]["url"] = url;
                        openaiBlocks.push_back(imageBlock);
                    }
                }
                // convert audio to openai input_audio format
                else if (blockType == "audio")
                {
                    auto data = block.value("data", "");
                    auto format = block.value("format", "wav");

                    if (!data.empty())
                    {
                        nlohmann::json audioBlock;
                        audioBlock["type"] = "input_audio";
                        audioBlock["input_audio"]["data"] = data;
                        audioBlock["input_audio"]["format"] = format;
                        openaiBlocks.push_back(audioBlock);
                    }
                }
            }

            m["content"] = openaiBlocks;
        }
        // plain message passthrough
        else
        {
            m["content"] = msg.content;
        }

        messages.push_back(m);

        if (!pendingImageMessage.is_null())
        {
            messages.push_back(pendingImageMessage);
        }
    }

    // sanitize and validate message ordering
    sanitizeMessages(messages);
    ProviderHelper::sanitizeToolCallInputs(messages);
    messages = validateTranscript(messages);

    body["messages"] = messages;

    // a model the capability table marks as non-function-calling rejects a tools payload, so drop it with a clear warning
    if (!request.tools.empty() && !ModelCapabilities::supportsFunctionCalling(request.model))
    {
        spdlog::warn("[OpenAiProvider] Model '{}' does not support function calling, dropping {} tool(s) from the request", request.model, request.tools.size());
    }
    // wrap tools in openai function format
    else if (!request.tools.empty())
    {
        auto toolsJson = nlohmann::json::array();

        for (const auto &tool : request.tools)
        {
            if (tool.contains("type") && tool["type"] == "function")
            {
                toolsJson.push_back(tool);
            }
            else
            {
                nlohmann::json wrapped;
                wrapped["type"] = "function";
                wrapped["function"] = tool;
                toolsJson.push_back(wrapped);
            }
        }

        body["tools"] = toolsJson;
    }

    return body;
}

ChatCompletionResponse OpenAiProvider::parseResponse(const nlohmann::json &response) const
{
    ChatCompletionResponse result;

    // bail out if no choices present
    if (!response.contains("choices") || !response["choices"].is_array() || response["choices"].empty())
    {
        return result;
    }

    // extract first choice message
    const auto &choice = response["choices"][0];

    if (!choice.contains("message"))
    {
        return result;
    }

    const auto &message = choice["message"];

    // content is explicitly null on a tool-call response, so read it defensively instead of via value()
    result.content = stringField(message, "content");

    // parse reasoning_content (o1, o3 models)
    result.reasoningContent = stringField(message, "reasoning_content");

    // finish_reason with fallback to "stop"
    auto fr = stringField(choice, "finish_reason");
    result.finishReason = fr.empty() ? "stop" : fr;

    // parse tool calls from response
    if (message.contains("tool_calls") && message["tool_calls"].is_array())
    {
        for (const auto &tc : message["tool_calls"])
        {
            ToolCall toolCall;
            toolCall.id = ProviderHelper::sanitizeToolCallId(stringField(tc, "id"));

            if (tc.contains("function"))
            {
                toolCall.name = stringField(tc["function"], "name");
                auto argsStr = stringField(tc["function"], "arguments");
                toolCall.arguments = ProviderHelper::repairJsonArgs(argsStr);
            }

            result.toolCalls.push_back(toolCall);
        }
    }

    // extract usage data
    if (response.contains("usage"))
    {
        result.usage = response["usage"];
    }

    return result;
}

ChatCompletionResponse OpenAiProvider::chat(const ChatCompletionRequest &request)
{
    // configure http client with auth headers
    util::HttpClient client(baseUrl, timeout);
    client.setHeader("Authorization", "Bearer " + apiKey);
    client.setHeader("Content-Type", "application/json");

    for (const auto &[key, value] : extraHeaders)
    {
        client.setHeader(key, value);
    }

    // send request and check for errors
    auto body = buildRequestBody(request);
    auto httpResponse = client.post("/chat/completions", body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));

    if (httpResponse.statusCode < 200 || httpResponse.statusCode >= 300)
    {
        auto safeMsg = ProviderHelper::sanitizeErrorMessage(httpResponse.body);
        auto errorType = ProviderHelper::classifyError(safeMsg);
        spdlog::error("[OpenAiProvider] API error (HTTP {}, type={}): {}", httpResponse.statusCode, errorType, safeMsg);
        throw std::runtime_error("[OpenAiProvider] OpenAI API error (HTTP " + std::to_string(httpResponse.statusCode) + ", " + errorType + "): " + safeMsg);
    }

    // parse and return response
    auto json = nlohmann::json::parse(httpResponse.body, nullptr, false);

    if (json.is_discarded())
    {
        throw std::runtime_error("[OpenAiProvider] OpenAI API returned invalid JSON (transient)");
    }

    return parseResponse(json);
}

void OpenAiProvider::chatStream(const ChatCompletionRequest &request, StreamCallback callback, const CancelPredicate &isCancelled)
{
    // configure http client with auth headers
    util::HttpClient client(baseUrl, timeout);
    client.setHeader("Authorization", "Bearer " + apiKey);
    client.setHeader("Content-Type", "application/json");

    for (const auto &[key, value] : extraHeaders)
    {
        client.setHeader(key, value);
    }

    // build request body with streaming enabled
    auto body = buildRequestBody(request);
    body["stream"] = true;

    // state for accumulating tool calls across stream chunks
    struct PendingToolCall
    {
        std::string id;
        std::string name;
        std::string arguments;
    };

    std::map<int, PendingToolCall> pendingToolCalls;
    std::string actualFinishReason;
    bool doneEmitted = false;

    // clang-format off
    client.postStream("/chat/completions", body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace), [&](const std::string &data) {
        // handle empty lines and stream termination
        if (data.empty() || data == "[DONE]")
        {
            if (data == "[DONE]" && !doneEmitted)
            {
                // flush any pending tool calls
                if (!pendingToolCalls.empty() && actualFinishReason != "length")
                {
                    for (auto &[index, ptc] : pendingToolCalls)
                    {
                        ToolCall tc;
                        tc.id = ProviderHelper::sanitizeToolCallId(ptc.id);
                        tc.name = ptc.name;
                        tc.arguments = ProviderHelper::repairJsonArgs(ptc.arguments);

                        StreamChunk chunk;
                        chunk.type = "tool_call";
                        chunk.toolCall = tc;
                        callback(chunk);
                    }

                    pendingToolCalls.clear();
                }

                // emit done event with finish reason
                StreamChunk chunk;
                chunk.type = "done";
                chunk.finishReason = actualFinishReason.empty() ? "stop" : actualFinishReason;
                callback(chunk);
                doneEmitted = true;
            }

            return;
        }

        try
        {
            auto json = nlohmann::json::parse(data);

            if (!json.contains("choices") || !json["choices"].is_array() || json["choices"].empty())
            {
                // openai-compatible servers deliver in-band failures as data:{"error":...} over http 200, so surface them instead of ending as a clean stop
                if (json.contains("error"))
                {
                    const auto &err = json["error"];
                    auto errMsg = err.is_object() && err.contains("message") && err["message"].is_string() ? err["message"].get<std::string>() : err.dump();
                    throw std::runtime_error("Provider stream error: " + errMsg);
                }

                // could be a usage-only message
                if (json.contains("usage"))
                {
                    StreamChunk chunk;
                    chunk.type = "usage";
                    chunk.usage = json["usage"];
                    callback(chunk);
                }

                return;
            }

            const auto &choice = json["choices"][0];
            const auto &delta = choice.value("delta", nlohmann::json::object());

            // handle content delta
            if (delta.contains("content") && !delta["content"].is_null())
            {
                auto content = delta.value("content", "");

                if (!content.empty())
                {
                    StreamChunk chunk;
                    chunk.type = "content";
                    chunk.content = content;
                    callback(chunk);
                }
            }

            // handle reasoning_content delta (o1, o3 models)
            if (delta.contains("reasoning_content") && !delta["reasoning_content"].is_null())
            {
                auto reasoning = delta.value("reasoning_content", "");

                if (!reasoning.empty())
                {
                    StreamChunk chunk;
                    chunk.type = "thinking";
                    chunk.content = reasoning;
                    callback(chunk);
                }
            }

            // handle tool call deltas
            if (delta.contains("tool_calls") && delta["tool_calls"].is_array())
            {
                static constexpr int MAX_TOOL_CALL_INDEX = 128;

                for (const auto &tc : delta["tool_calls"])
                {
                    auto indexIt = tc.find("index");
                    int index = indexIt != tc.end() && indexIt->is_number_integer() ? indexIt->get<int>() : 0;

                    if (index < 0 || index >= MAX_TOOL_CALL_INDEX)
                    {
                        spdlog::warn("[OpenAiProvider] Ignoring tool call with out-of-range index: {}", index);
                        continue;
                    }

                    // accumulate tool call id
                    if (tc.contains("id") && tc["id"].is_string() && !tc["id"].get<std::string>().empty())
                    {
                        pendingToolCalls[index].id = tc.value("id", "");
                    }

                    // accumulate function name and arguments
                    if (tc.contains("function"))
                    {
                        // name arrives complete in the first delta, set once
                        if (pendingToolCalls[index].name.empty())
                        {
                            pendingToolCalls[index].name = stringField(tc["function"], "name");
                        }

                        pendingToolCalls[index].arguments += stringField(tc["function"], "arguments");
                    }
                }
            }

            // track the actual finish reason
            if (choice.contains("finish_reason") && !choice["finish_reason"].is_null())
            {
                auto finishReason = choice.value("finish_reason", "");

                if (!finishReason.empty())
                {
                    actualFinishReason = finishReason;
                }
            }
        }
        catch (const nlohmann::json::exception &)
        {
            // non-JSON data received, likely an HTTP error page or plain-text error
            spdlog::warn("[OpenAiProvider] Non-JSON stream data received: {}", ionclaw::util::StringHelper::utf8SafeTruncate(data, 200));
            throw std::runtime_error("[OpenAiProvider] Non-JSON error response: " + ionclaw::util::StringHelper::utf8SafeTruncate(data, 500));
        }
    }, isCancelled);
    // clang-format on

    // some openai-compatible servers close the stream without a [DONE] sentinel, so flush any pending state on stream end
    if (!doneEmitted)
    {
        if (!pendingToolCalls.empty() && actualFinishReason != "length")
        {
            for (auto &[index, ptc] : pendingToolCalls)
            {
                ToolCall tc;
                tc.id = ProviderHelper::sanitizeToolCallId(ptc.id);
                tc.name = ptc.name;
                tc.arguments = ProviderHelper::repairJsonArgs(ptc.arguments);

                StreamChunk chunk;
                chunk.type = "tool_call";
                chunk.toolCall = tc;
                callback(chunk);
            }
        }

        StreamChunk chunk;
        chunk.type = "done";
        chunk.finishReason = actualFinishReason.empty() ? "stop" : actualFinishReason;
        callback(chunk);
        doneEmitted = true;
    }
}

} // namespace provider
} // namespace ionclaw
