#pragma once

#include <functional>
#include <memory>
#include <mutex>

#include "ionclaw/config/Config.hpp"

namespace ionclaw
{
namespace config
{

class ConfigStore
{
public:
    explicit ConfigStore(const Config &initial);

    // returns the current immutable config under lock
    std::shared_ptr<const Config> snapshot() const;

    // swaps in a new immutable config under lock
    void replace(const Config &next);

    // copies the current config applies the mutator and replaces it under lock
    void update(const std::function<void(Config &)> &mutator);

private:
    mutable std::mutex mutex;
    std::shared_ptr<const Config> current;
};

} // namespace config
} // namespace ionclaw
