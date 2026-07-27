#pragma once

#include <string>
#include <vector>

#include "ionclaw/embedding/EmbeddingProvider.hpp"

namespace ionclaw
{
namespace embedding
{

class OpenAiEmbeddingProvider final : public EmbeddingProvider
{
public:
    explicit OpenAiEmbeddingProvider(const std::string &providerName);

    std::string providerName() const override;
    std::vector<std::vector<float>> embed(const std::vector<std::string> &texts, const EmbeddingContext &context) const override;

private:
    std::string name;

    static std::string stripModelPrefix(const std::string &model);
    static std::string embeddingsEndpoint(const std::string &baseUrl);
};

} // namespace embedding
} // namespace ionclaw
