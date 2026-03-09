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

class OpenAiProvider : public LlmProvider
{
public:
    OpenAiProvider(const std::string &apiKey, const std::string &baseUrl = "https://api.openai.com/v1",
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
    ChatCompletionResponse parseResponse(const nlohmann::json &response) const;

    // message processing
    static void sanitizeMessages(nlohmann::json &messages);
    static nlohmann::json validateTranscript(const nlohmann::json &messages);
};

} // namespace provider
} // namespace ionclaw
