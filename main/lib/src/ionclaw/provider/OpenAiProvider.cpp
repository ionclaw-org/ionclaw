#include "ionclaw/provider/OpenAiProvider.hpp"

#include <algorithm>
#include <regex>
#include <stdexcept>

#include "ionclaw/provider/ProviderHelper.hpp"
#include "ionclaw/util/HttpClient.hpp"
#include "ionclaw/util/UniqueId.hpp"
#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace provider
{

// sanitize messages before sending to OpenAI API
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

// validate message transcript ordering:
// - merge consecutive same-role messages (when no tool_calls involved)
// - drop orphaned tool results that don't follow an assistant message with tool_calls
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

            if (lastNonTool &&
                (*lastNonTool).value("role", "") == "assistant" &&
                (*lastNonTool).contains("tool_calls") &&
                (*lastNonTool)["tool_calls"].is_array())
            {
                validated.push_back(msg);
            }
            else
            {
                spdlog::debug("[OpenAiProvider] Dropping orphaned tool result (tool_call_id={})",
                              msg.value("tool_call_id", "?"));
            }

            continue;
        }

        // merge consecutive user messages
        if (role == "user")
        {
            if (!validated.empty() && validated.back().value("role", "") == "user")
            {
                auto &prev = validated.back();

                if (prev.contains("content") && prev["content"].is_string() &&
                    msg.contains("content") && msg["content"].is_string())
                {
                    prev["content"] = prev["content"].get<std::string>() + "\n\n" +
                                      msg["content"].get<std::string>();
                }
                else
                {
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
            if (!validated.empty() &&
                validated.back().value("role", "") == "assistant" &&
                !validated.back().contains("tool_calls") &&
                !msg.contains("tool_calls"))
            {
                auto &prev = validated.back();
                auto prevContent = prev.value("content", "");
                auto curContent = msg.value("content", "");

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
                validated.push_back(msg);
            }

            continue;
        }

        validated.push_back(msg);
    }

    return validated;
}

OpenAiProvider::OpenAiProvider(const std::string &apiKey, const std::string &baseUrl, int timeout,
                               const std::map<std::string, std::string> &extraHeaders)
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

nlohmann::json OpenAiProvider::buildRequestBody(const ChatCompletionRequest &request) const
{
    // set base request fields
    nlohmann::json body;
    body["model"] = ProviderHelper::stripProviderPrefix(request.model);
    body["temperature"] = request.temperature;
    body["max_tokens"] = request.maxTokens;

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

            body[key] = value;
        }
    }

    // apply thinking/reasoning budget (for o1, o3, Claude with extended thinking via OpenAI-compat)
    if (!thinkingLevel.empty() && thinkingLevel != "off")
    {
        // openrouter: inject reasoning.effort instead of thinking block
        bool isOpenRouter = baseUrl.find("openrouter") != std::string::npos;

        if (isOpenRouter)
        {
            // skip reasoning injection for openrouter/auto and x-ai/grok models
            auto modelLower = request.model;
            std::transform(modelLower.begin(), modelLower.end(), modelLower.begin(),
                           [](unsigned char c)
                           { return c < 0x80 ? static_cast<unsigned char>(std::tolower(c)) : c; });
            bool skipReasoning = modelLower.find("openrouter/auto") != std::string::npos ||
                                 modelLower.find("x-ai/grok") != std::string::npos;

            if (!skipReasoning)
            {
                // map thinking level to openrouter reasoning.effort
                // adaptive → medium, off → none, others pass through
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

                // remove legacy flat reasoning_effort that conflicts with nested format
                body.erase("reasoning_effort");
            }
        }
        else
        {
            std::map<std::string, int> budgets = {{"low", 2048}, {"medium", 8192}, {"high", 32768}};
            auto it = budgets.find(thinkingLevel);
            int budget = (it != budgets.end()) ? it->second : 8192;

            body["thinking"] = {{"type", "enabled"}, {"budget_tokens", budget}};
            body["temperature"] = 1; // required for extended thinking
        }
    }

    // convert messages to openai format
    auto messages = nlohmann::json::array();

    for (const auto &msg : request.messages)
    {
        nlohmann::json m;
        m["role"] = msg.role;

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

            // use content blocks with image when available
            if (msg.contentBlocks.is_array() && !msg.contentBlocks.empty())
            {
                auto openaiBlocks = nlohmann::json::array();

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
                        openaiBlocks.push_back(imageBlock);
                    }
                    else if (blockType == "text")
                    {
                        nlohmann::json textBlock;
                        textBlock["type"] = "text";
                        textBlock["text"] = block.value("text", "");
                        openaiBlocks.push_back(textBlock);
                    }
                }

                m["content"] = openaiBlocks;
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
    }

    // sanitize and validate message ordering
    sanitizeMessages(messages);
    ProviderHelper::sanitizeToolCallInputs(messages);
    messages = validateTranscript(messages);

    body["messages"] = messages;

    // wrap tools in openai function format
    if (!request.tools.empty())
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

    result.content = message.value("content", "");

    // parse reasoning_content (o1, o3 models)
    if (message.contains("reasoning_content") && !message["reasoning_content"].is_null())
    {
        result.reasoningContent = message.value("reasoning_content", "");
    }

    // finish_reason with fallback to "stop"
    auto fr = choice.value("finish_reason", "");
    result.finishReason = fr.empty() ? "stop" : fr;

    // parse tool calls from response
    if (message.contains("tool_calls") && message["tool_calls"].is_array())
    {
        for (const auto &tc : message["tool_calls"])
        {
            ToolCall toolCall;
            toolCall.id = ProviderHelper::sanitizeToolCallId(tc.value("id", ""));

            if (tc.contains("function"))
            {
                toolCall.name = tc["function"].value("name", "");
                auto argsStr = tc["function"].value("arguments", "");
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
        throw std::runtime_error("OpenAI API error (HTTP " + std::to_string(httpResponse.statusCode) + ", " + errorType + "): " + safeMsg);
    }

    // parse and return response
    auto json = nlohmann::json::parse(httpResponse.body);
    return parseResponse(json);
}

void OpenAiProvider::chatStream(const ChatCompletionRequest &request, StreamCallback callback)
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

    client.postStream("/chat/completions", body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace), [&](const std::string &data)
                      {
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

            if (!json.contains("choices") || json["choices"].empty())
            {
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
                for (const auto &tc : delta["tool_calls"])
                {
                    auto index = tc.value("index", 0);

                    // accumulate tool call id
                    if (tc.contains("id") && tc["id"].is_string() && !tc["id"].get<std::string>().empty())
                    {
                        pendingToolCalls[index].id = tc.value("id", "");
                    }

                    // accumulate function name and arguments
                    if (tc.contains("function"))
                    {
                        // name arrives complete in the first delta, set once
                        if (tc["function"].contains("name") && pendingToolCalls[index].name.empty())
                        {
                            pendingToolCalls[index].name = tc["function"].value("name", "");
                        }

                        if (tc["function"].contains("arguments"))
                        {
                            pendingToolCalls[index].arguments += tc["function"].value("arguments", "");
                        }
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
        catch (const std::exception &e)
        {
            spdlog::debug("[OpenAiProvider] Stream parse error: {}", e.what());
        } });
}

} // namespace provider
} // namespace ionclaw
