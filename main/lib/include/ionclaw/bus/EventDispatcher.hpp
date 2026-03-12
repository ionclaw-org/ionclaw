#pragma once

#include <functional>
#include <map>
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
    void addNamedHandler(const std::string &id, EventHandler handler);
    void removeHandler(const std::string &id);
    void broadcast(const std::string &eventType, const nlohmann::json &data);

private:
    std::vector<EventHandler> handlers;
    std::map<std::string, EventHandler> namedHandlers;
    std::mutex mutex;
};

} // namespace bus
} // namespace ionclaw
