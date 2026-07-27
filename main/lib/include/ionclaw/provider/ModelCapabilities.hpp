#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace ionclaw
{
namespace provider
{

// one model's capabilities and limits, mirroring litellm's model_prices_and_context_window entries
struct ModelInfo
{
    std::string provider;
    int64_t maxInputTokens = 0;
    bool supportsVision = false;
    bool supportsFunctionCalling = false;
    bool supportsReasoning = false;
    bool supportsPromptCaching = false;
};

// litellm-equivalent capability table loaded from the embedded model_capabilities.json
class ModelCapabilities
{
public:
    // returns the record for a model (exact key, then after stripping a provider/ prefix), or nullopt if unknown
    static std::optional<ModelInfo> lookup(const std::string &model);

    // capability queries default an unknown model to permissive so a valid but unlisted model is never silently degraded
    static bool supportsVision(const std::string &model, bool defaultIfUnknown = true);
    static bool supportsReasoning(const std::string &model, bool defaultIfUnknown = true);
    static bool supportsFunctionCalling(const std::string &model, bool defaultIfUnknown = true);

    // prompt caching defaults to false for an unknown model, since a wrongly-sent cache_control can be rejected
    static bool supportsPromptCaching(const std::string &model, bool defaultIfUnknown = false);

    // context window (max input tokens) for a model, or 0 if unknown
    static int64_t contextWindow(const std::string &model);

private:
    static void ensureLoaded();
    static std::once_flag loadFlag;
    static std::unordered_map<std::string, ModelInfo> table;
};

} // namespace provider
} // namespace ionclaw
