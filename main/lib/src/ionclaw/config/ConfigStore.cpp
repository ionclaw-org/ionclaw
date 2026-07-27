#include "ionclaw/config/ConfigStore.hpp"

namespace ionclaw
{
namespace config
{

ConfigStore::ConfigStore(const Config &initial)
    : current(std::make_shared<const Config>(initial))
{
}

std::shared_ptr<const Config> ConfigStore::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return current;
}

void ConfigStore::replace(const Config &next)
{
    auto updated = std::make_shared<const Config>(next);
    std::lock_guard<std::mutex> lock(mutex);
    current = std::move(updated);
}

void ConfigStore::update(const std::function<void(Config &)> &mutator)
{
    std::lock_guard<std::mutex> lock(mutex);
    Config copy = *current;
    mutator(copy);
    current = std::make_shared<const Config>(std::move(copy));
}

} // namespace config
} // namespace ionclaw
