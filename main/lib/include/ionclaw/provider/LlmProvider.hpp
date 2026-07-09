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
    nlohmann::json contentBlocks;
    std::string reasoningContent;
    // provider-native reasoning blocks replayed verbatim, e.g. anthropic thinking blocks with their signature
    nlohmann::json reasoningBlocks;
};

struct ChatCompletionRequest
{
    std::vector<Message> messages;
    std::string model;
    double temperature = 0.7;
    int maxTokens = 4096;
    std::vector<nlohmann::json> tools;
    nlohmann::json modelParams;
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
    // provider-native reasoning blocks to replay on the next tool-loop turn, e.g. anthropic thinking blocks with their signature
    nlohmann::json reasoningBlocks;
    std::vector<ToolCall> toolCalls;
    std::string finishReason;
    nlohmann::json usage;
};

using StreamCallback = std::function<void(const StreamChunk &chunk)>;
using CancelPredicate = std::function<bool()>;

class LlmProvider
{
public:
    virtual ~LlmProvider() = default;
    virtual ChatCompletionResponse chat(const ChatCompletionRequest &request) = 0;
    virtual void chatStream(const ChatCompletionRequest &request, StreamCallback callback, const CancelPredicate &isCancelled = {}) = 0;
    virtual std::string name() const = 0;
};

} // namespace provider
} // namespace ionclaw
