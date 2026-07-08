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
    std::string mode;
    int64_t maxInputTokens = 0;
    int64_t maxOutputTokens = 0;
    int64_t maxTokens = 0;
    double inputCostPerToken = 0.0;
    double outputCostPerToken = 0.0;
    bool supportsVision = false;
    bool supportsFunctionCalling = false;
    bool supportsToolChoice = false;
    bool supportsReasoning = false;
    bool supportsPdfInput = false;
    bool supportsResponseSchema = false;
    bool supportsPromptCaching = false;
};

// litellm-equivalent capability table loaded from the embedded model_capabilities.json
class ModelCapabilities
{
public:
    // returns the record for a model (exact key, then after stripping a provider/ prefix), or nullopt if unknown
    static std::optional<ModelInfo> lookup(const std::string &model);

    // capability queries; an unknown model defaults to permissive so a valid but unlisted model is never silently degraded
    static bool supportsVision(const std::string &model, bool defaultIfUnknown = true);
    static bool supportsReasoning(const std::string &model, bool defaultIfUnknown = true);
    static bool supportsFunctionCalling(const std::string &model, bool defaultIfUnknown = true);

    // context window (max input tokens) for a model, or 0 if unknown
    static int64_t contextWindow(const std::string &model);

private:
    static void ensureLoaded();
    static std::once_flag loadFlag;
    static std::unordered_map<std::string, ModelInfo> table;
};

} // namespace provider
} // namespace ionclaw
