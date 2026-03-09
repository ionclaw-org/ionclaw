#include "ionclaw/transcription/TranscriptionProviderRegistry.hpp"

#include "ionclaw/transcription/LocalTranscriptionProvider.hpp"
#include "ionclaw/transcription/OpenAITranscriptionProvider.hpp"

namespace ionclaw
{
namespace transcription
{

TranscriptionProviderRegistry::TranscriptionProviderRegistry()
{
    registerProvider("openai", std::make_unique<OpenAITranscriptionProvider>("openai"));
    registerProvider("grok", std::make_unique<OpenAITranscriptionProvider>("grok"));
    registerProvider("local", std::make_unique<LocalTranscriptionProvider>("local"));
}

TranscriptionProviderRegistry &TranscriptionProviderRegistry::instance()
{
    static TranscriptionProviderRegistry inst;
    return inst;
}

void TranscriptionProviderRegistry::registerProvider(const std::string &name,
                                                     std::unique_ptr<TranscriptionProvider> provider)
{
    if (provider)
    {
        providers_[name] = std::move(provider);
    }
}

TranscriptionProvider *TranscriptionProviderRegistry::get(const std::string &name) const
{
    auto it = providers_.find(name);

    if (it != providers_.end())
    {
        return it->second.get();
    }

    return nullptr;
}

std::vector<std::string> TranscriptionProviderRegistry::providerNames() const
{
    std::vector<std::string> names;
    names.reserve(providers_.size());

    for (const auto &p : providers_)
    {
        names.push_back(p.first);
    }

    return names;
}

} // namespace transcription
} // namespace ionclaw
