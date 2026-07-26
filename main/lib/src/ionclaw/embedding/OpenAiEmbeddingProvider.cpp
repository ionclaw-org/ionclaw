#include "ionclaw/embedding/OpenAiEmbeddingProvider.hpp"

#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

#include "ionclaw/config/Config.hpp"
#include "ionclaw/util/HttpClient.hpp"
#include "ionclaw/util/StringHelper.hpp"

namespace ionclaw
{
namespace embedding
{

OpenAiEmbeddingProvider::OpenAiEmbeddingProvider(const std::string &providerName)
    : name(providerName)
{
}

std::string OpenAiEmbeddingProvider::providerName() const
{
    return name;
}

std::string OpenAiEmbeddingProvider::stripModelPrefix(const std::string &model)
{
    auto pos = model.find('/');

    if (pos != std::string::npos)
    {
        return model.substr(pos + 1);
    }

    return model;
}

std::string OpenAiEmbeddingProvider::embeddingsEndpoint(const std::string &baseUrl)
{
    auto base = baseUrl;

    while (!base.empty() && base.back() == '/')
    {
        base.pop_back();
    }

    // a base url that already carries a version segment points at the api root, otherwise the openai v1 root is appended
    if (base.find("/v1") != std::string::npos)
    {
        return base + "/embeddings";
    }

    return base + "/v1/embeddings";
}

std::vector<std::vector<float>> OpenAiEmbeddingProvider::embed(const std::vector<std::string> &texts, const EmbeddingContext &context) const
{
    if (!context.config)
    {
        spdlog::error("[OpenAiEmbeddingProvider] no config available");
        return {};
    }

    if (texts.empty())
    {
        return {};
    }

    auto apiKey = context.config->resolveApiKey(name);

    if (apiKey.empty())
    {
        spdlog::error("[OpenAiEmbeddingProvider] no API key for provider '{}'", name);
        return {};
    }

    auto baseUrl = context.config->resolveBaseUrl(name);

    if (baseUrl.empty())
    {
        spdlog::error("[OpenAiEmbeddingProvider] no base url for provider '{}'", name);
        return {};
    }

    auto endpoint = embeddingsEndpoint(baseUrl);
    auto modelId = stripModelPrefix(context.model);

    nlohmann::json body;
    body["model"] = modelId;
    body["input"] = texts;

    std::map<std::string, std::string> headers;
    headers["Content-Type"] = "application/json";
    headers["Authorization"] = "Bearer " + apiKey;

    auto resp = ionclaw::util::HttpClient::request("POST", endpoint, headers, body.dump(), 60);

    if (resp.statusCode < 200 || resp.statusCode >= 300)
    {
        spdlog::error("[OpenAiEmbeddingProvider] API returned HTTP {}: {}", resp.statusCode, ionclaw::util::StringHelper::utf8SafeTruncate(resp.body, 500));
        return {};
    }

    auto json = nlohmann::json::parse(resp.body, nullptr, false);

    if (json.is_discarded() || !json.contains("data") || !json["data"].is_array())
    {
        spdlog::error("[OpenAiEmbeddingProvider] malformed embeddings response");
        return {};
    }

    // the api returns one entry per input carrying its position, so place each vector at its own index
    std::vector<std::vector<float>> vectors(json["data"].size());

    for (const auto &entry : json["data"])
    {
        auto index = entry.value("index", -1);

        if (index < 0 || index >= static_cast<int>(vectors.size()) || !entry.contains("embedding") || !entry["embedding"].is_array())
        {
            spdlog::error("[OpenAiEmbeddingProvider] embeddings response entry is out of range or missing its vector");
            return {};
        }

        std::vector<float> vec;
        vec.reserve(entry["embedding"].size());

        for (const auto &value : entry["embedding"])
        {
            if (!value.is_number())
            {
                spdlog::error("[OpenAiEmbeddingProvider] embeddings response contains a non-numeric element");
                return {};
            }

            vec.push_back(value.get<float>());
        }

        vectors[index] = std::move(vec);
    }

    return vectors;
}

} // namespace embedding
} // namespace ionclaw
