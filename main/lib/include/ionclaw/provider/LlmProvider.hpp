#pragma once

#include <functional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace ionclaw
{
namespace provider
{

struct ToolCall
{
    std::string id;
    std::string name;
    nlohmann::json arguments;
};

struct Message
{
    std::string role;
    std::string content;
    std::vector<ToolCall> toolCalls;
    std::string toolCallId;
    std::string name;
    nlohmann::json contentBlocks; // array of content blocks for multimodal (text + images)
    std::string reasoningContent; // extended thinking content
};

struct ChatCompletionRequest
{
    std::vector<Message> messages;
    std::string model;
    double temperature = 0.7;
    int maxTokens = 4096;
    std::vector<nlohmann::json> tools;
    bool stream = false;
    nlohmann::json modelParams; // provider-specific params from config
};

struct StreamChunk
{
    std::string type;
    std::string content;
    ToolCall toolCall;
    nlohmann::json usage;
    std::string finishReason;
};

struct ChatCompletionResponse
{
    std::string content;
    std::string reasoningContent;
    std::vector<ToolCall> toolCalls;
    std::string finishReason;
    nlohmann::json usage;
};

using StreamCallback = std::function<void(const StreamChunk &chunk)>;

class LlmProvider
{
public:
    virtual ~LlmProvider() = default;
    virtual ChatCompletionResponse chat(const ChatCompletionRequest &request) = 0;
    virtual void chatStream(const ChatCompletionRequest &request, StreamCallback callback) = 0;
    virtual std::string name() const = 0;
};

} // namespace provider
} // namespace ionclaw
