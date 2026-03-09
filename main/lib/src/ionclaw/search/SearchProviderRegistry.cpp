#include "ionclaw/search/SearchProviderRegistry.hpp"

#include "ionclaw/search/BraveSearchProvider.hpp"
#include "ionclaw/search/DuckDuckGoSearchProvider.hpp"

namespace ionclaw
{
namespace search
{

SearchProviderRegistry::SearchProviderRegistry()
{
    registerProvider("brave", std::make_unique<BraveSearchProvider>());
    registerProvider("duckduckgo", std::make_unique<DuckDuckGoSearchProvider>());
}

SearchProviderRegistry &SearchProviderRegistry::instance()
{
    static SearchProviderRegistry inst;
    return inst;
}

void SearchProviderRegistry::registerProvider(const std::string &name,
                                              std::unique_ptr<SearchProvider> provider)
{
    if (provider)
    {
        providers_[name] = std::move(provider);
    }
}

SearchProvider *SearchProviderRegistry::get(const std::string &providerName) const
{
    auto it = providers_.find(providerName);
    if (it != providers_.end())
    {
        return it->second.get();
    }
    return nullptr;
}

std::vector<std::string> SearchProviderRegistry::providerNames() const
{
    std::vector<std::string> names;
    names.reserve(providers_.size());
    for (const auto &p : providers_)
    {
        names.push_back(p.first);
    }
    return names;
}

} // namespace search
} // namespace ionclaw
