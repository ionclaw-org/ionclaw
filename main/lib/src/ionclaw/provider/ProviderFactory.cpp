#include "ionclaw/provider/ProviderFactory.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>

#include "ionclaw/provider/AnthropicProvider.hpp"
#include "ionclaw/provider/FailoverProvider.hpp"
#include "ionclaw/provider/LlamaProvider.hpp"
#include "ionclaw/provider/OpenAiProvider.hpp"
#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace provider
{

namespace
{

// llama runs a local gguf file pointed to by base_url, so it bypasses the credential-based flow
std::shared_ptr<LlmProvider> makeLlamaProvider(const std::string &modelPath, const nlohmann::json &params)
{
#ifdef IONCLAW_HAS_LLAMA_CPP
    if (modelPath.empty())
    {
        throw std::runtime_error("[ProviderFactory] llama provider requires a model file path in 'base_url'");
    }

    return std::make_shared<LlamaProvider>(modelPath, params);
#else
    (void)modelPath;
    (void)params;
    throw std::runtime_error("[ProviderFactory] llama provider requires building with -DIONCLAW_LLAMA_CPP=ON");
#endif
}

} // namespace

std::string ProviderFactory::defaultBaseUrl(const std::string &providerName)
{
    static const std::map<std::string, std::string> defaults = {
        {"anthropic", "https://api.anthropic.com"},
        {"openai", "https://api.openai.com/v1"},
        {"openrouter", "https://openrouter.ai/api/v1"},
        {"deepseek", "https://api.deepseek.com/v1"},
        {"grok", "https://api.x.ai/v1"},
        {"google", "https://generativelanguage.googleapis.com/v1beta/openai"},
        {"gemini", "https://generativelanguage.googleapis.com/v1beta/openai"},
        {"kimi", "https://api.moonshot.cn/v1"},
        {"moonshot", "https://api.moonshot.cn/v1"},
    };

    auto it = defaults.find(providerName);
    return it != defaults.end() ? it->second : "";
}

std::shared_ptr<LlmProvider> ProviderFactory::create(const std::string &providerName, const std::string &apiKey, const std::string &baseUrl, int timeout, const std::map<std::string, std::string> &extraHeaders)
{
    // resolve base url from provider name if not explicitly set
    auto resolvedUrl = baseUrl.empty() ? defaultBaseUrl(providerName) : baseUrl;

    // anthropic uses its own native api
    if (providerName == "anthropic")
    {
        return std::make_shared<AnthropicProvider>(apiKey, resolvedUrl, timeout, extraHeaders);
    }

    // all other providers use OpenAI-compatible API
    if (providerName == "openai" || providerName == "openrouter" || providerName == "deepseek" || providerName == "grok" || providerName == "google" || providerName == "gemini" || providerName == "kimi" || providerName == "moonshot")
    {
        return std::make_shared<OpenAiProvider>(apiKey, resolvedUrl, timeout, extraHeaders);
    }

    // fallback: assume OpenAI-compatible for unknown providers (custom base_url)
    if (!resolvedUrl.empty())
    {
        return std::make_shared<OpenAiProvider>(apiKey, resolvedUrl, timeout, extraHeaders);
    }

    throw std::runtime_error("[ProviderFactory] Unknown provider: " + providerName);
}

std::shared_ptr<LlmProvider> ProviderFactory::createFromModel(const std::string &model, const ionclaw::config::Config &config)
{
    // resolve provider config from model string
    auto providerConfig = config.resolveProvider(model);
    auto providerName = providerConfig.name;

    // extract provider name from model prefix if not configured
    if (providerName.empty())
    {
        auto slashPos = model.find('/');

        if (slashPos != std::string::npos)
        {
            providerName = model.substr(0, slashPos);
        }
        else
        {
            providerName = model;
        }
    }

    // llama loads a local model file and carries its setup through model_params
    if (providerName == "llama")
    {
        return makeLlamaProvider(providerConfig.baseUrl, providerConfig.modelParams);
    }

    // resolve credentials and settings
    auto apiKey = config.resolveApiKey(providerName);
    auto baseUrl = providerConfig.baseUrl;
    auto timeout = providerConfig.timeout;
    auto headers = providerConfig.requestHeaders;

    return create(providerName, apiKey, baseUrl, timeout, headers);
}

std::shared_ptr<LlmProvider> ProviderFactory::createFailoverFromProfiles(const std::vector<ionclaw::config::ProfileConfig> &profiles, const std::string &defaultModel, const ionclaw::config::Config &config)
{
    // sort by priority (lower = higher priority)
    auto sorted = profiles;

    // clang-format off
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return a.priority < b.priority; });
    // clang-format on

    std::vector<std::shared_ptr<LlmProvider>> providers;
    std::vector<std::string> names;
    std::vector<nlohmann::json> profileParams;

    for (const auto &profile : sorted)
    {
        auto model = profile.model.empty() ? defaultModel : profile.model;

        try
        {
            std::shared_ptr<LlmProvider> provider;

            if (!profile.credential.empty())
            {
                // resolve credential directly
                auto credIt = config.credentials.find(profile.credential);
                std::string apiKey;

                if (credIt != config.credentials.end())
                {
                    apiKey = credIt->second.key.empty() ? credIt->second.token : credIt->second.key;
                }

                auto slashPos = model.find('/');
                auto providerName = slashPos != std::string::npos ? model.substr(0, slashPos) : model;
                auto baseUrl = profile.baseUrl;

                provider = create(providerName, apiKey, baseUrl);
            }
            else
            {
                provider = createFromModel(model, config);
            }

            providers.push_back(provider);
            names.push_back(model);
            profileParams.push_back(profile.modelParams);

            spdlog::info("[ProviderFactory] Added failover profile: {} (priority {})", model, profile.priority);
        }
        catch (const std::exception &e)
        {
            spdlog::warn("[ProviderFactory] Failed to create profile for '{}': {}", model, e.what());
        }
    }

    if (providers.empty())
    {
        throw std::runtime_error("[ProviderFactory] No valid profiles for failover provider");
    }

    // skip FailoverProvider wrapper only if single profile has no custom model params
    if (providers.size() == 1 && (!profileParams[0].is_object() || profileParams[0].empty()))
    {
        return providers[0];
    }

    return std::make_shared<FailoverProvider>(std::move(providers), std::move(names), std::move(profileParams));
}

} // namespace provider
} // namespace ionclaw
