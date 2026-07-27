#include "ionclaw/embedding/EmbeddingProviderRegistry.hpp"

#include "ionclaw/embedding/OpenAiEmbeddingProvider.hpp"

#ifdef IONCLAW_HAS_LLAMA_CPP
#include "ionclaw/embedding/LlamaEmbeddingProvider.hpp"
#endif

namespace ionclaw
{
namespace embedding
{

EmbeddingProviderRegistry::EmbeddingProviderRegistry()
{
    registerProvider("openai", std::make_unique<OpenAiEmbeddingProvider>("openai"));
    registerProvider("gemini", std::make_unique<OpenAiEmbeddingProvider>("gemini"));
    registerProvider("google", std::make_unique<OpenAiEmbeddingProvider>("google"));
    registerProvider("mistral", std::make_unique<OpenAiEmbeddingProvider>("mistral"));

#ifdef IONCLAW_HAS_LLAMA_CPP
    registerProvider("llama", std::make_unique<LlamaEmbeddingProvider>("llama"));
#endif
}

EmbeddingProviderRegistry &EmbeddingProviderRegistry::instance()
{
    static EmbeddingProviderRegistry inst;
    return inst;
}

void EmbeddingProviderRegistry::registerProvider(const std::string &name, std::unique_ptr<EmbeddingProvider> provider)
{
    if (provider)
    {
        providers[name] = std::move(provider);
    }
}

EmbeddingProvider *EmbeddingProviderRegistry::get(const std::string &name) const
{
    auto it = providers.find(name);

    if (it != providers.end())
    {
        return it->second.get();
    }

    return nullptr;
}

std::vector<std::string> EmbeddingProviderRegistry::providerNames() const
{
    std::vector<std::string> names;
    names.reserve(providers.size());

    for (const auto &p : providers)
    {
        names.push_back(p.first);
    }

    return names;
}

} // namespace embedding
} // namespace ionclaw
