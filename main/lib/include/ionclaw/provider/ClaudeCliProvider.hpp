#pragma once

#include <string>

#include "ionclaw/provider/LlmProvider.hpp"

namespace ionclaw
{
namespace provider
{

class ClaudeCliProvider final : public LlmProvider
{
public:
    ClaudeCliProvider(const std::string &model, int timeout = 180);

    ChatCompletionResponse chat(const ChatCompletionRequest &request) override;
    void chatStream(const ChatCompletionRequest &request, StreamCallback callback, const CancelPredicate &isCancelled = {}) override;
    std::string name() const override;

private:
    std::string model;
    int timeout;

    std::string flattenMessages(const ChatCompletionRequest &request) const;
    ChatCompletionResponse invokeCli(const std::string &prompt, const CancelPredicate &isCancelled) const;
};

} // namespace provider
} // namespace ionclaw
