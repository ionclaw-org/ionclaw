#include "ionclaw/provider/ProviderFactory.hpp"

#include <algorithm>
#include <stdexcept>

#include "ionclaw/provider/AnthropicProvider.hpp"
#include "ionclaw/provider/FailoverProvider.hpp"
#include "ionclaw/provider/OpenAiProvider.hpp"
#ifdef IONCLAW_HAS_LLAMA_CPP
#include "ionclaw/provider/LlamaProvider.hpp"
#endif
#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace provider
{

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

std::shared_ptr<LlmProvider> ProviderFactory::create(const std::string &providerName, const std::string &apiKey,
                                                     const std::string &baseUrl, int timeout,
                                                     const std::map<std::string, std::string> &extraHeaders)
{
    auto resolvedUrl = baseUrl.empty() ? defaultBaseUrl(providerName) : baseUrl;

    if (providerName == "anthropic")
    {
        return std::make_shared<AnthropicProvider>(apiKey, resolvedUrl, timeout, extraHeaders);
    }

#ifdef IONCLAW_HAS_LLAMA_CPP
    // apiKey holds the model file path for llama
    if (providerName == "llama")
    {
        return std::make_shared<LlamaProvider>(apiKey);
    }
#else
    if (providerName == "llama")
    {
        throw std::runtime_error("llama provider requires building with -DIONCLAW_LLAMA_CPP=ON");
    }
#endif

    if (providerName == "openai" || providerName == "openrouter" ||
        providerName == "deepseek" || providerName == "grok" || providerName == "google" ||
        providerName == "gemini" || providerName == "kimi" || providerName == "moonshot")
    {
        return std::make_shared<OpenAiProvider>(apiKey, resolvedUrl, timeout, extraHeaders);
    }

    // unknown provider with a custom base_url: assume openai-compatible
    if (!resolvedUrl.empty())
    {
        return std::make_shared<OpenAiProvider>(apiKey, resolvedUrl, timeout, extraHeaders);
    }

    throw std::runtime_error("Unknown provider: " + providerName);
}

std::shared_ptr<LlmProvider> ProviderFactory::createFromModel(const std::string &model, const ionclaw::config::Config &config)
{
    auto providerConfig = config.resolveProvider(model);
    auto providerName = providerConfig.name;

    // extract provider name from the model string prefix (e.g. "anthropic/claude-3")
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

#ifdef IONCLAW_HAS_LLAMA_CPP
    // llama uses base_url as the model file path, no api key needed
    if (providerName == "llama")
    {
        auto modelPath = providerConfig.baseUrl;

        if (modelPath.empty())
        {
            throw std::runtime_error("llama provider requires 'base_url' pointing to the .gguf model file");
        }

        return std::make_shared<LlamaProvider>(modelPath, providerConfig.modelParams);
    }
#else
    if (providerName == "llama")
    {
        throw std::runtime_error("llama provider requires building with -DIONCLAW_LLAMA_CPP=ON");
    }
#endif

    auto apiKey = config.resolveApiKey(providerName);
    auto baseUrl = providerConfig.baseUrl;
    auto timeout = providerConfig.timeout;
    auto headers = providerConfig.requestHeaders;

    return create(providerName, apiKey, baseUrl, timeout, headers);
}

std::shared_ptr<LlmProvider> ProviderFactory::createFailoverFromProfiles(
    const std::vector<ionclaw::config::ProfileConfig> &profiles,
    const std::string &defaultModel,
    const ionclaw::config::Config &config)
{
    // sort ascending so lower priority value = higher priority
    auto sorted = profiles;
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b)
              { return a.priority < b.priority; });

    std::vector<std::shared_ptr<LlmProvider>> providers;
    std::vector<std::string> names;
    std::vector<nlohmann::json> profileParams;

    for (const auto &profile : sorted)
    {
        auto model = profile.model.empty() ? defaultModel : profile.model;

        try
        {
            std::shared_ptr<LlmProvider> provider;

            auto slashPos = model.find('/');
            auto providerName = slashPos != std::string::npos ? model.substr(0, slashPos) : model;

#ifdef IONCLAW_HAS_LLAMA_CPP
            if (providerName == "llama")
            {
                auto modelPath = profile.baseUrl;

                if (modelPath.empty())
                {
                    auto pc = config.resolveProvider(model);
                    modelPath = pc.baseUrl;
                }

                if (modelPath.empty())
                {
                    throw std::runtime_error("llama profile requires model file path in base_url");
                }

                provider = std::make_shared<LlamaProvider>(modelPath, profile.modelParams);
            }
            else
#else
            if (providerName == "llama")
            {
                throw std::runtime_error("llama provider requires building with -DIONCLAW_LLAMA_CPP=ON");
            }
            else
#endif
                if (!profile.credential.empty())
            {
                auto credIt = config.credentials.find(profile.credential);
                std::string apiKey;

                if (credIt != config.credentials.end())
                {
                    apiKey = credIt->second.key.empty() ? credIt->second.token : credIt->second.key;
                }

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
        throw std::runtime_error("No valid profiles for failover provider");
    }

    // skip the failover wrapper when there is only one profile with no custom model params
    if (providers.size() == 1 &&
        (!profileParams[0].is_object() || profileParams[0].empty()))
    {
        return providers[0];
    }

    return std::make_shared<FailoverProvider>(std::move(providers), std::move(names), std::move(profileParams));
}

} // namespace provider
} // namespace ionclaw
