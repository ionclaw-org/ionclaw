#pragma once

#include <map>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "ionclaw/provider/LlmProvider.hpp"

namespace ionclaw
{
namespace provider
{

class AnthropicProvider : public LlmProvider
{
public:
    AnthropicProvider(const std::string &apiKey, const std::string &baseUrl = "https://api.anthropic.com",
                      int timeout = 60, const std::map<std::string, std::string> &extraHeaders = {});

    ChatCompletionResponse chat(const ChatCompletionRequest &request) override;
    void chatStream(const ChatCompletionRequest &request, StreamCallback callback) override;
    std::string name() const override;

private:
    std::string apiKey;
    std::string baseUrl;
    int timeout;
    std::map<std::string, std::string> extraHeaders;

    nlohmann::json buildRequestBody(const ChatCompletionRequest &request) const;
    nlohmann::json convertToolsToAnthropicFormat(const std::vector<nlohmann::json> &tools) const;
    ChatCompletionResponse parseResponse(const nlohmann::json &response) const;
    void parseStreamEvent(const std::string &eventType, const nlohmann::json &data,
                          StreamCallback &callback, std::vector<ToolCall> &pendingToolCalls,
                          std::string &currentToolCallId, std::string &currentToolCallName,
                          std::string &currentToolCallArgs) const;

    // message processing
    static void sanitizeMessages(nlohmann::json &messages);
    static nlohmann::json validateTranscript(const nlohmann::json &messages);
};

} // namespace provider
} // namespace ionclaw
