#include "ionclaw/provider/AnthropicProvider.hpp"

#include <regex>
#include <stdexcept>

#include "ionclaw/provider/ProviderHelper.hpp"
#include "ionclaw/util/HttpClient.hpp"
#include "ionclaw/util/StringHelper.hpp"
#include "ionclaw/util/UniqueId.hpp"
#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace provider
{

// sanitize messages before sending to Anthropic API
void AnthropicProvider::sanitizeMessages(nlohmann::json &messages)
{
    for (auto &msg : messages)
    {
        auto role = msg.value("role", "");

        // empty content for user/assistant messages
        if ((role == "user" || role == "assistant") && msg.contains("content"))
        {
            if (msg["content"].is_string() && msg["content"].get<std::string>().empty())
            {
                msg["content"] = "(empty)";
            }
        }
    }
}

// validate message transcript ordering for Anthropic API:
// - merge consecutive same-role messages (when safe to do so)
// - ensure user/assistant alternation
nlohmann::json AnthropicProvider::validateTranscript(const nlohmann::json &messages)
{
    nlohmann::json validated = nlohmann::json::array();

    for (const auto &msg : messages)
    {
        auto role = msg.value("role", "");

        // merge consecutive user messages
        if (role == "user")
        {
            if (!validated.empty() && validated.back().value("role", "") == "user")
            {
                auto &prev = validated.back();

                // only merge plain text content
                if (prev.contains("content") && prev["content"].is_string() &&
                    msg.contains("content") && msg["content"].is_string())
                {
                    prev["content"] = prev["content"].get<std::string>() + "\n\n" +
                                      msg["content"].get<std::string>();
                }
                else if (prev.contains("content") && prev["content"].is_array() &&
                         msg.contains("content") && msg["content"].is_array() &&
                         !prev["content"].empty() && prev["content"][0].value("type", "") == "tool_result" &&
                         !msg["content"].empty() && msg["content"][0].value("type", "") == "tool_result")
                {
                    // merge consecutive tool_result user messages
                    for (const auto &block : msg["content"])
                    {
                        prev["content"].push_back(block);
                    }
                }
                else
                {
                    // non-mergeable array content; insert placeholder to maintain alternation
                    nlohmann::json placeholder;
                    placeholder["role"] = "assistant";
                    placeholder["content"] = "(continued)";
                    validated.push_back(placeholder);
                    validated.push_back(msg);
                }
            }
            else
            {
                validated.push_back(msg);
            }

            continue;
        }

        // merge consecutive assistant messages (only plain text, not with content blocks containing tool_use)
        if (role == "assistant")
        {
            if (!validated.empty() && validated.back().value("role", "") == "assistant")
            {
                auto &prev = validated.back();
                bool prevHasToolUse = false;
                bool curHasToolUse = false;

                // check if previous message has tool_use blocks
                if (prev.contains("content") && prev["content"].is_array())
                {
                    for (const auto &block : prev["content"])
                    {
                        if (block.value("type", "") == "tool_use")
                        {
                            prevHasToolUse = true;
                            break;
                        }
                    }
                }

                // check if current message has tool_use blocks
                if (msg.contains("content") && msg["content"].is_array())
                {
                    for (const auto &block : msg["content"])
                    {
                        if (block.value("type", "") == "tool_use")
                        {
                            curHasToolUse = true;
                            break;
                        }
                    }
                }

                // merge only if neither has tool_use and both are plain text
                if (!prevHasToolUse && !curHasToolUse &&
                    prev.contains("content") && prev["content"].is_string() &&
                    msg.contains("content") && msg["content"].is_string())
                {
                    auto prevContent = prev["content"].get<std::string>();
                    auto curContent = msg["content"].get<std::string>();
                    prev["content"] = (prevContent.empty() ? curContent : prevContent + "\n\n" + curContent);
                }
                else
                {
                    // insert placeholder user message to maintain role alternation
                    validated.push_back({{"role", "user"}, {"content", "[continued]"}});
                    validated.push_back(msg);
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

AnthropicProvider::AnthropicProvider(const std::string &apiKey, const std::string &baseUrl, int timeout,
                                     const std::map<std::string, std::string> &extraHeaders)
    : apiKey(apiKey)
    , baseUrl(baseUrl)
    , timeout(timeout)
    , extraHeaders(extraHeaders)
{
}

std::string AnthropicProvider::name() const
{
    return "anthropic";
}

nlohmann::json AnthropicProvider::convertToolsToAnthropicFormat(const std::vector<nlohmann::json> &tools) const
{
    auto result = nlohmann::json::array();

    for (const auto &tool : tools)
    {
        nlohmann::json anthropicTool;
        anthropicTool["name"] = tool.value("name", "");
        anthropicTool["description"] = tool.value("description", "");

        // map parameters to input_schema
        if (tool.contains("parameters"))
        {
            anthropicTool["input_schema"] = tool["parameters"];
        }
        else if (tool.contains("function"))
        {
            anthropicTool["name"] = tool["function"].value("name", "");
            anthropicTool["description"] = tool["function"].value("description", "");

            if (tool["function"].contains("parameters"))
            {
                anthropicTool["input_schema"] = tool["function"]["parameters"];
            }
        }

        result.push_back(anthropicTool);
    }

    return result;
}

nlohmann::json AnthropicProvider::buildRequestBody(const ChatCompletionRequest &request) const
{
    // set base request fields
    nlohmann::json body;
    body["model"] = ProviderHelper::stripProviderPrefix(request.model);
    body["max_tokens"] = request.maxTokens;
    body["temperature"] = request.temperature;

    // apply model params from config
    std::string thinkingLevel;

    if (request.modelParams.is_object())
    {
        for (auto &[key, value] : request.modelParams.items())
        {
            if (key == "context_window")
            {
                continue;
            }

            if (key == "thinking")
            {
                thinkingLevel = value.is_string() ? value.get<std::string>() : "";
                continue;
            }

            body[key] = value;
        }
    }

    // apply extended thinking budget
    if (!thinkingLevel.empty() && thinkingLevel != "off")
    {
        std::map<std::string, int> budgets = {{"low", 2048}, {"medium", 8192}, {"high", 32768}};
        auto it = budgets.find(thinkingLevel);
        int budget = (it != budgets.end()) ? it->second : 8192;

        body["thinking"] = {{"type", "enabled"}, {"budget_tokens", budget}};
        body["temperature"] = 1; // required for Anthropic extended thinking
    }

    // convert messages to anthropic format
    std::string systemPrompt;
    auto messages = nlohmann::json::array();

    for (const auto &msg : request.messages)
    {
        // collect system messages into a single prompt
        if (msg.role == "system")
        {
            if (!systemPrompt.empty())
            {
                systemPrompt += "\n\n";
            }

            systemPrompt += msg.content;
            continue;
        }

        // convert tool results to anthropic format
        // merge consecutive tool results into a single user message
        if (msg.role == "tool")
        {
            nlohmann::json contentBlock;
            contentBlock["type"] = "tool_result";
            contentBlock["tool_use_id"] = msg.toolCallId;

            // use content blocks with image when available
            if (msg.contentBlocks.is_array() && !msg.contentBlocks.empty())
            {
                auto resultContent = nlohmann::json::array();

                for (const auto &block : msg.contentBlocks)
                {
                    auto blockType = block.value("type", "");

                    if (blockType == "image")
                    {
                        nlohmann::json imageBlock;
                        imageBlock["type"] = "image";
                        imageBlock["source"]["type"] = "base64";
                        imageBlock["source"]["media_type"] = block.value("media_type", "image/png");
                        imageBlock["source"]["data"] = block.value("data", "");
                        resultContent.push_back(imageBlock);
                    }
                    else if (blockType == "text")
                    {
                        nlohmann::json textBlock;
                        textBlock["type"] = "text";
                        textBlock["text"] = block.value("text", "");
                        resultContent.push_back(textBlock);
                    }
                }

                contentBlock["content"] = resultContent;
            }
            else
            {
                contentBlock["content"] = msg.content;
            }

            // append to previous user message if it already has tool_result blocks
            if (!messages.empty() && messages.back().value("role", "") == "user")
            {
                auto &prevContent = messages.back()["content"];
                if (prevContent.is_array() && !prevContent.empty() &&
                    prevContent[0].value("type", "") == "tool_result")
                {
                    prevContent.push_back(contentBlock);
                    continue;
                }
            }

            nlohmann::json toolResultMsg;
            toolResultMsg["role"] = "user";
            toolResultMsg["content"] = nlohmann::json::array({contentBlock});
            messages.push_back(toolResultMsg);
            continue;
        }

        // convert assistant messages with tool calls
        if (msg.role == "assistant" && !msg.toolCalls.empty())
        {
            nlohmann::json assistantMsg;
            assistantMsg["role"] = "assistant";

            auto contentBlocks = nlohmann::json::array();

            if (!msg.content.empty())
            {
                nlohmann::json textBlock;
                textBlock["type"] = "text";
                textBlock["text"] = msg.content;
                contentBlocks.push_back(textBlock);
            }

            for (const auto &tc : msg.toolCalls)
            {
                nlohmann::json toolUseBlock;
                toolUseBlock["type"] = "tool_use";
                toolUseBlock["id"] = tc.id;
                toolUseBlock["name"] = tc.name;
                toolUseBlock["input"] = tc.arguments;
                contentBlocks.push_back(toolUseBlock);
            }

            assistantMsg["content"] = contentBlocks;
            messages.push_back(assistantMsg);
            continue;
        }

        // handle multimodal content blocks (images, audio)
        if (msg.role == "user" && msg.contentBlocks.is_array() && !msg.contentBlocks.empty())
        {
            nlohmann::json m;
            m["role"] = "user";

            auto anthropicBlocks = nlohmann::json::array();

            for (const auto &block : msg.contentBlocks)
            {
                auto blockType = block.value("type", "");

                if (blockType == "text")
                {
                    anthropicBlocks.push_back(block);
                }
                else if (blockType == "image")
                {
                    auto url = block.value("url", "");

                    if (url.size() >= 5 && url.substr(0, 5) == "data:")
                    {
                        // data URI: extract media type and base64 data
                        auto commaPos = url.find(',');
                        auto semiPos = url.find(';');

                        if (commaPos != std::string::npos && semiPos != std::string::npos && semiPos >= 5 && semiPos < commaPos)
                        {
                            auto mediaType = url.substr(5, semiPos - 5);
                            auto data = url.substr(commaPos + 1);

                            nlohmann::json imageBlock;
                            imageBlock["type"] = "image";
                            imageBlock["source"]["type"] = "base64";
                            imageBlock["source"]["media_type"] = mediaType;
                            imageBlock["source"]["data"] = data;
                            anthropicBlocks.push_back(imageBlock);
                        }
                    }
                    else if (!url.empty())
                    {
                        // url-based image
                        nlohmann::json imageBlock;
                        imageBlock["type"] = "image";
                        imageBlock["source"]["type"] = "url";
                        imageBlock["source"]["url"] = url;
                        anthropicBlocks.push_back(imageBlock);
                    }
                }
                else if (blockType == "audio")
                {
                    // anthropic audio input: base64-encoded audio data
                    auto data = block.value("data", "");
                    auto format = block.value("format", "wav");

                    if (!data.empty())
                    {
                        // map format to MIME type
                        std::string mediaType = "audio/wav";

                        if (format == "mp3")
                        {
                            mediaType = "audio/mpeg";
                        }
                        else if (format == "ogg")
                        {
                            mediaType = "audio/ogg";
                        }
                        else if (format == "webm")
                        {
                            mediaType = "audio/webm";
                        }

                        nlohmann::json audioBlock;
                        audioBlock["type"] = "document";
                        audioBlock["source"]["type"] = "base64";
                        audioBlock["source"]["media_type"] = mediaType;
                        audioBlock["source"]["data"] = data;
                        anthropicBlocks.push_back(audioBlock);
                    }
                }
            }

            m["content"] = anthropicBlocks;
            messages.push_back(m);
            continue;
        }

        // plain message passthrough
        nlohmann::json m;
        m["role"] = msg.role;
        m["content"] = msg.content;
        messages.push_back(m);
    }

    // sanitize and validate message ordering
    sanitizeMessages(messages);
    ProviderHelper::sanitizeToolCallInputs(messages);
    messages = validateTranscript(messages);

    // set system prompt if present, with cache_control for prompt caching
    if (!systemPrompt.empty())
    {
        body["system"] = nlohmann::json::array({{{"type", "text"}, {"text", systemPrompt}, {"cache_control", {{"type", "ephemeral"}}}}});
    }

    body["messages"] = messages;

    // attach tools if provided
    if (!request.tools.empty())
    {
        body["tools"] = convertToolsToAnthropicFormat(request.tools);
    }

    return body;
}

ChatCompletionResponse AnthropicProvider::parseResponse(const nlohmann::json &response) const
{
    ChatCompletionResponse result;

    // extract content blocks from response
    if (response.contains("content") && response["content"].is_array())
    {
        for (const auto &block : response["content"])
        {
            auto blockType = block.value("type", "");

            if (blockType == "text")
            {
                if (!result.content.empty())
                {
                    result.content += "\n";
                }

                result.content += block.value("text", "");
            }
            else if (blockType == "thinking")
            {
                // extended thinking block
                if (!result.reasoningContent.empty())
                {
                    result.reasoningContent += "\n";
                }

                result.reasoningContent += block.value("thinking", "");
            }
            else if (blockType == "tool_use")
            {
                ToolCall tc;
                tc.id = ProviderHelper::sanitizeToolCallId(block.value("id", ""), "toolu_");
                tc.name = block.value("name", "");
                tc.arguments = block.value("input", nlohmann::json::object());
                result.toolCalls.push_back(tc);
            }
        }
    }

    // finish_reason with fallback to "stop"
    auto fr = response.value("stop_reason", "");
    result.finishReason = fr.empty() ? "stop" : fr;

    // extract usage data
    if (response.contains("usage"))
    {
        result.usage = response["usage"];
    }

    return result;
}

void AnthropicProvider::parseStreamEvent(const std::string &eventType, const nlohmann::json &data,
                                         StreamCallback &callback, std::vector<ToolCall> &pendingToolCalls,
                                         std::string &currentToolCallId, std::string &currentToolCallName,
                                         std::string &currentToolCallArgs) const
{
    // handle new content block (start of tool_use)
    if (eventType == "content_block_start")
    {
        if (data.contains("content_block"))
        {
            auto blockType = data["content_block"].value("type", "");

            if (blockType == "tool_use")
            {
                currentToolCallId = data["content_block"].value("id", "");
                currentToolCallName = data["content_block"].value("name", "");
                currentToolCallArgs.clear();
            }
        }
    }
    // handle content delta (text, thinking, or tool args)
    else if (eventType == "content_block_delta")
    {
        if (data.contains("delta"))
        {
            auto deltaType = data["delta"].value("type", "");

            if (deltaType == "text_delta")
            {
                StreamChunk chunk;
                chunk.type = "content";
                chunk.content = data["delta"].value("text", "");
                callback(chunk);
            }
            else if (deltaType == "thinking_delta")
            {
                StreamChunk chunk;
                chunk.type = "thinking";
                chunk.content = data["delta"].value("thinking", "");
                callback(chunk);
            }
            else if (deltaType == "input_json_delta")
            {
                currentToolCallArgs += data["delta"].value("partial_json", "");
            }
        }
    }
    // handle end of content block (flush pending tool call)
    else if (eventType == "content_block_stop")
    {
        if (!currentToolCallId.empty())
        {
            ToolCall tc;
            tc.id = ProviderHelper::sanitizeToolCallId(currentToolCallId, "toolu_");
            tc.name = currentToolCallName;
            tc.arguments = ProviderHelper::repairJsonArgs(currentToolCallArgs);

            pendingToolCalls.push_back(tc);

            StreamChunk chunk;
            chunk.type = "tool_call";
            chunk.toolCall = tc;
            callback(chunk);

            currentToolCallId.clear();
            currentToolCallName.clear();
            currentToolCallArgs.clear();
        }
    }
    // handle message delta (stop reason and usage)
    else if (eventType == "message_delta")
    {
        if (data.contains("delta"))
        {
            auto stopReason = data["delta"].value("stop_reason", "");

            if (!stopReason.empty())
            {
                StreamChunk chunk;
                chunk.type = "done";
                chunk.finishReason = stopReason;
                callback(chunk);
            }
        }

        if (data.contains("usage"))
        {
            StreamChunk chunk;
            chunk.type = "usage";
            chunk.usage = data["usage"];
            callback(chunk);
        }
    }
    // handle message stop — only emit done if message_delta didn't already
    else if (eventType == "message_stop")
    {
        // message_delta already emits a "done" chunk with the actual stop_reason;
        // only emit here as fallback when no stop_reason was received
    }
    // note: "error" events are intercepted in chatStream before reaching this function
}

ChatCompletionResponse AnthropicProvider::chat(const ChatCompletionRequest &request)
{
    // configure http client with auth headers
    util::HttpClient client(baseUrl, timeout);
    client.setHeader("x-api-key", apiKey);
    client.setHeader("anthropic-version", "2023-06-01");
    client.setHeader("content-type", "application/json");

    for (const auto &[key, value] : extraHeaders)
    {
        client.setHeader(key, value);
    }

    // send request and check for errors
    auto body = buildRequestBody(request);
    auto httpResponse = client.post("/v1/messages", body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));

    if (httpResponse.statusCode < 200 || httpResponse.statusCode >= 300)
    {
        auto safeMsg = ProviderHelper::sanitizeErrorMessage(httpResponse.body);
        auto errorType = ProviderHelper::classifyError(safeMsg);
        spdlog::error("[AnthropicProvider] API error (HTTP {}, type={}): {}", httpResponse.statusCode, errorType, safeMsg);
        throw std::runtime_error("Anthropic API error (HTTP " + std::to_string(httpResponse.statusCode) + ", " + errorType + "): " + safeMsg);
    }

    // parse and return response
    auto json = nlohmann::json::parse(httpResponse.body, nullptr, false);

    if (json.is_discarded())
    {
        throw std::runtime_error("Anthropic API returned invalid JSON (transient)");
    }

    return parseResponse(json);
}

void AnthropicProvider::chatStream(const ChatCompletionRequest &request, StreamCallback callback)
{
    // configure http client with auth headers
    util::HttpClient client(baseUrl, timeout);
    client.setHeader("x-api-key", apiKey);
    client.setHeader("anthropic-version", "2023-06-01");
    client.setHeader("content-type", "application/json");

    for (const auto &[key, value] : extraHeaders)
    {
        client.setHeader(key, value);
    }

    // build request body with streaming enabled
    auto body = buildRequestBody(request);
    body["stream"] = true;

    // state for accumulating tool calls across stream events
    std::vector<ToolCall> pendingToolCalls;
    std::string currentToolCallId;
    std::string currentToolCallName;
    std::string currentToolCallArgs;
    bool doneEmitted = false;

    // wrap callback to track done emission and emit fallback on message_stop
    StreamCallback wrappedCallback = [&](const StreamChunk &chunk)
    {
        if (chunk.type == "done")
        {
            doneEmitted = true;
        }

        callback(chunk);
    };

    client.postStream("/v1/messages", body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace), [&](const std::string &data)
                      {
        if (data.empty() || data == "[DONE]")
        {
            return;
        }

        try
        {
            auto json = nlohmann::json::parse(data);
            auto eventType = json.value("type", "");

            // propagate API error events as exceptions
            if (eventType == "error")
            {
                auto errorMsg = json.contains("error") && json["error"].is_object()
                                    ? json["error"].value("message", "Unknown stream error")
                                    : "Unknown stream error";
                throw std::runtime_error("[AnthropicProvider] Stream error: " + errorMsg);
            }

            if (!eventType.empty())
            {
                parseStreamEvent(eventType, json, wrappedCallback, pendingToolCalls,
                                 currentToolCallId, currentToolCallName, currentToolCallArgs);

                // emit done fallback on message_stop if message_delta didn't carry stop_reason
                if (eventType == "message_stop" && !doneEmitted)
                {
                    StreamChunk chunk;
                    chunk.type = "done";
                    chunk.finishReason = "stop";
                    callback(chunk);
                    doneEmitted = true;
                }
            }
        }
        catch (const nlohmann::json::exception &)
        {
            // non-JSON data received — likely an HTTP error page or plain-text error
            spdlog::warn("[AnthropicProvider] Non-JSON stream data received: {}", ionclaw::util::StringHelper::utf8SafeTruncate(data, 200));
            throw std::runtime_error("[AnthropicProvider] Non-JSON error response: " + ionclaw::util::StringHelper::utf8SafeTruncate(data, 500));
        } });
}

} // namespace provider
} // namespace ionclaw
