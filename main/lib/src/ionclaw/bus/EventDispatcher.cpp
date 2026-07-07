#include "ionclaw/bus/EventDispatcher.hpp"

#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace bus
{

thread_local int EventDispatcher::broadcastDepth = 0;

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

    // a handler removing itself mid-broadcast on this thread cannot wait for its own broadcast to finish
    if (broadcastDepth > 0)
    {
        return;
    }

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

    ++broadcastDepth;

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
        catch (...)
        {
            // never let a handler escape, or the broadcasting counter would leak and drain() would hang forever
            spdlog::error("[EventDispatcher] handler threw a non-standard exception");
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
        catch (...)
        {
            // never let a handler escape, or the broadcasting counter would leak and drain() would hang forever
            spdlog::error("[EventDispatcher] named handler threw a non-standard exception");
        }
    }

    --broadcastDepth;

    std::lock_guard<std::mutex> lock(mutex);

    if (--broadcasting == 0)
    {
        idle.notify_all();
    }
}

} // namespace bus
} // namespace ionclaw
