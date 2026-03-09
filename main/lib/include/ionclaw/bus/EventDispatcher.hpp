#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace ionclaw
{
namespace bus
{

using EventHandler = std::function<void(const std::string &eventType, const nlohmann::json &data)>;

class EventDispatcher
{
public:
    void addHandler(EventHandler handler);
    void broadcast(const std::string &eventType, const nlohmann::json &data);

private:
    std::vector<EventHandler> handlers;
    std::mutex mutex;
};

} // namespace bus
} // namespace ionclaw
