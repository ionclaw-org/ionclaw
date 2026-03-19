#include "ionclaw/provider/FailoverProvider.hpp"

#include <algorithm>
#include <random>
#include <stdexcept>
#include <thread>

#include "ionclaw/provider/ProviderHelper.hpp"
#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace provider
{

FailoverProvider::FailoverProvider(
    std::vector<std::shared_ptr<LlmProvider>> providers,
    std::vector<std::string> providerNames,
    std::vector<nlohmann::json> profileModelParams)
    : providers(std::move(providers))
    , providerNames(std::move(providerNames))
    , profileModelParams(std::move(profileModelParams))
    , maxRetries(computeMaxRetries())
    , cooldownUntil(this->providers.size(), std::chrono::steady_clock::time_point{})
{
    if (this->providers.empty())
    {
        throw std::invalid_argument("[FailoverProvider] At least one provider is required");
    }

    if (this->providers.size() != this->providerNames.size())
    {
        throw std::invalid_argument("[FailoverProvider] Providers and provider names must have equal size");
    }

    // pad profileModelParams to match providers count if needed
    this->profileModelParams.resize(this->providers.size());
}

int FailoverProvider::computeMaxRetries() const
{
    auto count = static_cast<int>(providers.size());
    return std::min(160, std::max(32, 24 + count * 8));
}

std::string FailoverProvider::name() const
{
    auto idx = currentIndex.load();

    if (idx < providerNames.size())
    {
        return providerNames[idx];
    }

    return "failover";
}

bool FailoverProvider::isFailoverableError(const std::string &errorCategory)
{
    // context_overflow and billing are NOT failoverable:
    // context is too large for any provider, billing is an account issue
    return errorCategory == "auth" ||
           errorCategory == "rate_limit" ||
           errorCategory == "model_not_found" ||
           errorCategory == "timeout" ||
           errorCategory == "transient" ||
           errorCategory == "host_not_found";
}

void FailoverProvider::markGood(size_t idx)
{
    std::lock_guard<std::mutex> lock(cooldownMutex);

    if (idx < cooldownUntil.size())
    {
        cooldownUntil[idx] = std::chrono::steady_clock::time_point{};
    }
}

void FailoverProvider::markBad(size_t idx)
{
    std::lock_guard<std::mutex> lock(cooldownMutex);

    if (idx < cooldownUntil.size())
    {
        cooldownUntil[idx] = std::chrono::steady_clock::now() + std::chrono::seconds(COOLDOWN_SECONDS);
        spdlog::info("[FailoverProvider] Provider '{}' cooldown for {}s", providerNames[idx], COOLDOWN_SECONDS);
    }
}

bool FailoverProvider::isCoolingDown(size_t idx) const
{
    std::lock_guard<std::mutex> lock(cooldownMutex);

    if (idx >= cooldownUntil.size())
    {
        return false;
    }

    return std::chrono::steady_clock::now() < cooldownUntil[idx];
}

size_t FailoverProvider::findAvailableProvider() const
{
    auto current = currentIndex.load();

    // try current first
    if (!isCoolingDown(current))
    {
        return current;
    }

    // scan for non-cooling-down provider
    for (size_t i = 0; i < providers.size(); ++i)
    {
        auto idx = (current + i + 1) % providers.size();

        if (!isCoolingDown(idx))
        {
            return idx;
        }
    }

    // all cooling down: return current anyway
    return current;
}

// compute backoff delay with exponential growth and jitter
int FailoverProvider::computeBackoffMs(int consecutiveFailures, const std::string &errorCategory)
{
    static thread_local std::mt19937 rng(std::random_device{}());

    auto baseMs = (errorCategory == "rate_limit") ? 2000 : 1000;
    auto shift = std::min(std::max(consecutiveFailures - 1, 0), 3);
    auto delayMs = std::min(baseMs << shift, MAX_BACKOFF_MS);

    std::uniform_int_distribution<int> jitter(0, JITTER_MS);
    return delayMs + jitter(rng);
}

ChatCompletionRequest FailoverProvider::applyProfileParams(const ChatCompletionRequest &request, size_t profileIdx) const
{
    if (profileIdx >= profileModelParams.size() ||
        !profileModelParams[profileIdx].is_object() ||
        profileModelParams[profileIdx].empty())
    {
        return request;
    }

    // profile params override the request's modelParams
    auto modified = request;
    auto merged = modified.modelParams.is_object() ? modified.modelParams : nlohmann::json::object();
    merged.merge_patch(profileModelParams[profileIdx]);
    modified.modelParams = merged;

    return modified;
}

ChatCompletionResponse FailoverProvider::chat(const ChatCompletionRequest &request)
{
    int attempts = 0;
    int consecutiveFailures = 0;

    // use local index tracking so concurrent requests don't interfere
    auto idx = findAvailableProvider();

    while (attempts < maxRetries)
    {
        try
        {
            auto profileRequest = applyProfileParams(request, idx);
            auto response = providers[idx]->chat(profileRequest);
            markGood(idx);
            currentIndex.store(idx);
            return response;
        }
        catch (const std::exception &e)
        {
            auto category = ProviderHelper::classifyError(e.what());
            spdlog::warn("[FailoverProvider] Provider '{}' failed ({}): {}", providerNames[idx], category, e.what());

            if (isFailoverableError(category) && providers.size() > 1)
            {
                markBad(idx);
                consecutiveFailures++;

                auto backoffMs = computeBackoffMs(consecutiveFailures, category);
                spdlog::info("[FailoverProvider] Backing off {}ms before retry (attempt {})", backoffMs, attempts + 1);
                std::this_thread::sleep_for(std::chrono::milliseconds(backoffMs));

                idx = (idx + 1) % providers.size();

                if (isCoolingDown(idx))
                {
                    idx = findAvailableProvider();
                }

                spdlog::info("[FailoverProvider] Switching to provider '{}'", providerNames[idx]);
                attempts++;
                continue;
            }

            throw;
        }
    }

    throw std::runtime_error("All provider profiles exhausted after " + std::to_string(attempts) + " attempts");
}

void FailoverProvider::chatStream(const ChatCompletionRequest &request, StreamCallback callback)
{
    int attempts = 0;
    int consecutiveFailures = 0;
    bool contentDelivered = false;

    // wrap callback to track whether any content has been delivered to the consumer
    // only set contentDelivered on actual content chunks, not metadata (usage/done)
    auto wrappedCallback = [&callback, &contentDelivered](const StreamChunk &chunk)
    {
        if (chunk.type == "content" || chunk.type == "tool_call" || chunk.type == "thinking")
        {
            contentDelivered = true;
        }

        callback(chunk);
    };

    // use local index tracking so concurrent requests don't interfere
    auto idx = findAvailableProvider();

    while (attempts < maxRetries)
    {
        try
        {
            auto profileRequest = applyProfileParams(request, idx);
            providers[idx]->chatStream(profileRequest, wrappedCallback);
            markGood(idx);
            currentIndex.store(idx);
            return;
        }
        catch (const std::exception &e)
        {
            auto category = ProviderHelper::classifyError(e.what());
            spdlog::warn("[FailoverProvider] Provider '{}' stream failed ({}): {}", providerNames[idx], category, e.what());

            // if content was already delivered to the consumer, retrying would cause
            // duplicate partial content — propagate the error instead
            if (contentDelivered)
            {
                spdlog::warn("[FailoverProvider] Cannot retry: content already delivered to consumer");
                throw;
            }

            if (isFailoverableError(category) && providers.size() > 1)
            {
                markBad(idx);
                consecutiveFailures++;

                auto backoffMs = computeBackoffMs(consecutiveFailures, category);
                spdlog::info("[FailoverProvider] Backing off {}ms before stream retry (attempt {})", backoffMs, attempts + 1);
                std::this_thread::sleep_for(std::chrono::milliseconds(backoffMs));

                idx = (idx + 1) % providers.size();

                if (isCoolingDown(idx))
                {
                    idx = findAvailableProvider();
                }

                spdlog::info("[FailoverProvider] Switching to provider '{}'", providerNames[idx]);
                attempts++;
                continue;
            }

            throw;
        }
    }

    throw std::runtime_error("All provider profiles exhausted after " + std::to_string(attempts) + " stream attempts");
}

} // namespace provider
} // namespace ionclaw
