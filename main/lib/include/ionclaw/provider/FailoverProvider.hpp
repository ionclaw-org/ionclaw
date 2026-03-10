#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "ionclaw/provider/LlmProvider.hpp"

namespace ionclaw
{
namespace provider
{

class FailoverProvider : public LlmProvider
{
public:
    FailoverProvider(std::vector<std::shared_ptr<LlmProvider>> providers,
                     std::vector<std::string> providerNames,
                     std::vector<nlohmann::json> profileModelParams = {});

    ChatCompletionResponse chat(const ChatCompletionRequest &request) override;
    void chatStream(const ChatCompletionRequest &request, StreamCallback callback) override;
    std::string name() const override;

private:
    std::vector<std::shared_ptr<LlmProvider>> providers;
    std::vector<std::string> providerNames;
    std::vector<nlohmann::json> profileModelParams;
    std::atomic<size_t> currentIndex{0};
    int maxRetries;

    // cooldown tracking per provider index
    std::vector<std::chrono::steady_clock::time_point> cooldownUntil;
    mutable std::mutex cooldownMutex;
    static constexpr int COOLDOWN_SECONDS = 60;
    static constexpr int MAX_BACKOFF_MS = 8000;
    static constexpr int JITTER_MS = 500;

    ChatCompletionRequest applyProfileParams(const ChatCompletionRequest &request, size_t profileIdx) const;
    static bool isFailoverableError(const std::string &errorCategory);
    static int computeBackoffMs(int consecutiveFailures, const std::string &errorCategory);
    int computeMaxRetries() const;
    void markGood(size_t idx);
    void markBad(size_t idx);
    bool isCoolingDown(size_t idx) const;
    size_t findAvailableProvider() const;
};

} // namespace provider
} // namespace ionclaw
