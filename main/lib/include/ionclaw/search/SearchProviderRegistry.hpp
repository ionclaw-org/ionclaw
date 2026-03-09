#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ionclaw/search/SearchProvider.hpp"

namespace ionclaw
{
namespace search
{

class SearchProviderRegistry
{
public:
    static SearchProviderRegistry &instance();

    void registerProvider(const std::string &name, std::unique_ptr<SearchProvider> provider);
    SearchProvider *get(const std::string &providerName) const;
    std::vector<std::string> providerNames() const;

private:
    SearchProviderRegistry();
    std::unordered_map<std::string, std::unique_ptr<SearchProvider>> providers_;
};

} // namespace search
} // namespace ionclaw
