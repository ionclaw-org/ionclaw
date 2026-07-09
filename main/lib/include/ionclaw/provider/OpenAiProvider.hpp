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

class OpenAiProvider final : public LlmProvider
{
public:
    OpenAiProvider(const std::string &apiKey, const std::string &baseUrl = "https://api.openai.com/v1", int timeout = 60, const std::map<std::string, std::string> &extraHeaders = {});

    ChatCompletionResponse chat(const ChatCompletionRequest &request) override;
    void chatStream(const ChatCompletionRequest &request, StreamCallback callback, const CancelPredicate &isCancelled = {}) override;
    std::string name() const override;

private:
    std::string apiKey;
    std::string baseUrl;
    int timeout;
    std::map<std::string, std::string> extraHeaders;

    nlohmann::json buildRequestBody(const ChatCompletionRequest &request) const;
    ChatCompletionResponse parseResponse(const nlohmann::json &response) const;

    static void sanitizeMessages(nlohmann::json &messages);
    static nlohmann::json validateTranscript(const nlohmann::json &messages);

    // reads a string field, returning the fallback when the key is absent, null, or not a string
    // json::value() only substitutes the fallback for an absent key and throws type_error.302 on an explicit null
    static std::string stringField(const nlohmann::json &obj, const std::string &key, const std::string &fallback = "");

    // openai reasoning models (o1/o3/o4/gpt-5 class) reject max_tokens and non-default temperature
    static bool isReasoningModel(const std::string &model);

    // grants the test suite access to the stateless request/response transforms
    friend struct OpenAiProviderTestAccess;
};

} // namespace provider
} // namespace ionclaw
