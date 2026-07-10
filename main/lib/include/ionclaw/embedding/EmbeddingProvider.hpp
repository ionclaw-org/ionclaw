#pragma once

#include <string>
#include <vector>

namespace ionclaw
{
namespace config
{
struct Config;
}

namespace embedding
{

struct EmbeddingContext
{
    std::string model;
    std::string providerName;
    const ionclaw::config::Config *config = nullptr;
};

class EmbeddingProvider
{
public:
    virtual ~EmbeddingProvider() = default;

    virtual std::string providerName() const = 0;
    virtual std::vector<std::vector<float>> embed(const std::vector<std::string> &texts, const EmbeddingContext &context) const = 0;
};

} // namespace embedding
} // namespace ionclaw
