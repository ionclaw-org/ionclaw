#include "ionclaw/bus/EventDispatcher.hpp"

#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace bus
{

void EventDispatcher::addHandler(EventHandler handler)
{
    std::lock_guard<std::mutex> lock(mutex);
    handlers.push_back(std::move(handler));
}

void EventDispatcher::addNamedHandler(const std::string &id, EventHandler handler)
{
    std::lock_guard<std::mutex> lock(mutex);
    namedHandlers[id] = std::move(handler);
}

void EventDispatcher::removeHandler(const std::string &id)
{
    std::lock_guard<std::mutex> lock(mutex);
    namedHandlers.erase(id);
}

void EventDispatcher::broadcast(const std::string &eventType, const nlohmann::json &data)
{
    std::vector<EventHandler> handlersCopy;
    std::vector<EventHandler> namedCopy;

    {
        std::lock_guard<std::mutex> lock(mutex);
        handlersCopy = handlers;
        for (const auto &[id, h] : namedHandlers)
        {
            namedCopy.push_back(h);
        }
    }

    for (const auto &handler : handlersCopy)
    {
        try
        {
            handler(eventType, data);
        }
        catch (const std::exception &e)
        {
            spdlog::error("[EventDispatcher] handler exception: {}", e.what());
        }
    }

    for (const auto &handler : namedCopy)
    {
        try
        {
            handler(eventType, data);
        }
        catch (const std::exception &e)
        {
            spdlog::error("[EventDispatcher] named handler exception: {}", e.what());
        }
    }
}

} // namespace bus
} // namespace ionclaw
