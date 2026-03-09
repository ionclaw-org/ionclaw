#pragma once

#include <string>

#include "ionclaw/config/Config.hpp"

namespace ionclaw
{
namespace search
{

class SearchProvider
{
public:
    virtual ~SearchProvider() = default;

    virtual std::string name() const = 0;
    virtual std::string search(const std::string &query, int count, const ionclaw::config::CredentialConfig &credential) const = 0;
};

} // namespace search
} // namespace ionclaw
