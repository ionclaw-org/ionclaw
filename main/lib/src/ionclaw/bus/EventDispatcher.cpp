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

void EventDispatcher::broadcast(const std::string &eventType, const nlohmann::json &data)
{
    std::vector<EventHandler> handlersCopy;

    {
        std::lock_guard<std::mutex> lock(mutex);
        handlersCopy = handlers;
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
}

} // namespace bus
} // namespace ionclaw
