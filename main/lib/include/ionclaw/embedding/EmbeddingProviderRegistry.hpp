#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ionclaw/embedding/EmbeddingProvider.hpp"

namespace ionclaw
{
namespace embedding
{

class EmbeddingProviderRegistry
{
public:
    static EmbeddingProviderRegistry &instance();

    void registerProvider(const std::string &name, std::unique_ptr<EmbeddingProvider> provider);
    EmbeddingProvider *get(const std::string &name) const;
    std::vector<std::string> providerNames() const;

private:
    EmbeddingProviderRegistry();
    std::unordered_map<std::string, std::unique_ptr<EmbeddingProvider>> providers;
};

} // namespace embedding
} // namespace ionclaw
