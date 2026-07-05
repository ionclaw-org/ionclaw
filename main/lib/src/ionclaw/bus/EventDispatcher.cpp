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
    std::unique_lock<std::mutex> lock(mutex);
    namedHandlers.erase(id);

    // wait for in-flight broadcasts to drain so a removed handler is never invoked on a destroyed owner
    idle.wait(lock, [this]() { return broadcasting == 0; });
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
        ++broadcasting;
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

    std::lock_guard<std::mutex> lock(mutex);

    if (--broadcasting == 0)
    {
        idle.notify_all();
    }
}

} // namespace bus
} // namespace ionclaw
