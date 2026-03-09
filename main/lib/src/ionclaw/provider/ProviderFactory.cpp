#include "ionclaw/provider/ProviderFactory.hpp"

#include <algorithm>
#include <stdexcept>

#include "ionclaw/provider/AnthropicProvider.hpp"
#include "ionclaw/provider/FailoverProvider.hpp"
#include "ionclaw/provider/OpenAiProvider.hpp"
#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace provider
{

// resolve default base url for known providers
std::string ProviderFactory::defaultBaseUrl(const std::string &providerName)
{
    if (providerName == "anthropic")
    {
        return "https://api.anthropic.com";
    }
    else if (providerName == "openai")
    {
        return "https://api.openai.com/v1";
    }
    else if (providerName == "openrouter")
    {
        return "https://openrouter.ai/api/v1";
    }
    else if (providerName == "deepseek")
    {
        return "https://api.deepseek.com/v1";
    }
    else if (providerName == "grok")
    {
        return "https://api.x.ai/v1";
    }
    else if (providerName == "google" || providerName == "gemini")
    {
        return "https://generativelanguage.googleapis.com/v1beta/openai";
    }
    else if (providerName == "kimi" || providerName == "moonshot")
    {
        return "https://api.moonshot.cn/v1";
    }

    return "";
}

// create provider instance by name
std::shared_ptr<LlmProvider> ProviderFactory::create(const std::string &providerName, const std::string &apiKey,
                                                     const std::string &baseUrl, int timeout,
                                                     const std::map<std::string, std::string> &extraHeaders)
{
    // resolve base url from provider name if not explicitly set
    auto resolvedUrl = baseUrl.empty() ? defaultBaseUrl(providerName) : baseUrl;

    // anthropic uses its own native api
    if (providerName == "anthropic")
    {
        return std::make_shared<AnthropicProvider>(apiKey, resolvedUrl, timeout, extraHeaders);
    }

    // all other providers use OpenAI-compatible API
    if (providerName == "openai" || providerName == "openrouter" ||
        providerName == "deepseek" || providerName == "grok" || providerName == "google" ||
        providerName == "gemini" || providerName == "kimi" || providerName == "moonshot")
    {
        return std::make_shared<OpenAiProvider>(apiKey, resolvedUrl, timeout, extraHeaders);
    }

    // fallback: assume OpenAI-compatible for unknown providers (custom base_url)
    if (!resolvedUrl.empty())
    {
        return std::make_shared<OpenAiProvider>(apiKey, resolvedUrl, timeout, extraHeaders);
    }

    throw std::runtime_error("Unknown provider: " + providerName);
}

// create provider instance from model string and config
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

    // resolve credentials and settings
    auto apiKey = config.resolveApiKey(providerName);
    auto baseUrl = providerConfig.baseUrl;
    auto timeout = providerConfig.timeout;
    auto headers = providerConfig.requestHeaders;

    return create(providerName, apiKey, baseUrl, timeout, headers);
}

// create a failover provider from multiple profile configs
std::shared_ptr<LlmProvider> ProviderFactory::createFailoverFromProfiles(
    const std::vector<ionclaw::config::ProfileConfig> &profiles,
    const std::string &defaultModel,
    const ionclaw::config::Config &config)
{
    // sort by priority (lower = higher priority)
    auto sorted = profiles;
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b)
              { return a.priority < b.priority; });

    std::vector<std::shared_ptr<LlmProvider>> providers;
    std::vector<std::string> names;

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

            spdlog::info("[ProviderFactory] Added failover profile: {} (priority {})", model, profile.priority);
        }
        catch (const std::exception &e)
        {
            spdlog::warn("[ProviderFactory] Failed to create profile for '{}': {}", model, e.what());
        }
    }

    if (providers.empty())
    {
        throw std::runtime_error("No valid profiles for failover provider");
    }

    if (providers.size() == 1)
    {
        return providers[0];
    }

    return std::make_shared<FailoverProvider>(std::move(providers), std::move(names));
}

} // namespace provider
} // namespace ionclaw
