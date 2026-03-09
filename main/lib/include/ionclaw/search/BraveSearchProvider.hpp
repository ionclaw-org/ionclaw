#pragma once

#include "ionclaw/search/SearchProvider.hpp"

namespace ionclaw
{
namespace search
{

class BraveSearchProvider : public SearchProvider
{
public:
    std::string name() const override;
    std::string search(const std::string &query, int count, const ionclaw::config::CredentialConfig &credential) const override;
};

} // namespace search
} // namespace ionclaw
