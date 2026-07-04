#include "ionclaw/config/Config.hpp"

#include <stdexcept>

namespace ionclaw
{
namespace config
{

std::string Config::resolveApiKey(const std::string &providerName) const
{
    auto provIt = providers.find(providerName);

    if (provIt == providers.end())
    {
        return "";
    }

    const auto &provider = provIt->second;
    auto credIt = credentials.find(provider.credential);

    if (credIt == credentials.end())
    {
        return "";
    }

    // prefer key, fall back to token
    std::string key = credIt->second.key;

    if (!key.empty())
    {
        return key;
    }

    return credIt->second.token;
}

std::string Config::resolveBaseUrl(const std::string &providerName) const
{
    auto provIt = providers.find(providerName);

    if (provIt != providers.end() && !provIt->second.baseUrl.empty())
    {
        return provIt->second.baseUrl;
    }

    // defaults for known providers
    if (providerName == "anthropic")
    {
        return "https://api.anthropic.com";
    }

    if (providerName == "openai")
    {
        return "https://api.openai.com";
    }

    if (providerName == "google" || providerName == "gemini")
    {
        return "https://generativelanguage.googleapis.com/v1beta/openai";
    }

    return "";
}

const ProviderConfig *Config::findProvider(const std::string &model) const
{
    // extract prefix from model string (e.g., "anthropic/claude-3-5-sonnet" -> "anthropic")
    auto slashPos = model.find('/');
    auto prefix = slashPos != std::string::npos ? model.substr(0, slashPos) : model;

    // find provider matching the prefix
    auto provIt = providers.find(prefix);

    if (provIt != providers.end())
    {
        return &provIt->second;
    }

    // search all providers for a name match
    for (const auto &[key, provider] : providers)
    {
        if (provider.name == prefix)
        {
            return &provider;
        }
    }

    return nullptr;
}

ProviderConfig Config::resolveProvider(const std::string &model) const
{
    const auto *provider = findProvider(model);

    if (!provider)
    {
        throw std::runtime_error("[Config] No provider found for model: " + model);
    }

    return *provider;
}

} // namespace config
} // namespace ionclaw
