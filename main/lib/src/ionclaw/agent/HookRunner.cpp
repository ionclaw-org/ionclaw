#include "ionclaw/agent/HookRunner.hpp"

#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace agent
{

void HookRunner::registerHook(HookPoint point, HookCallback callback)
{
    std::lock_guard<std::mutex> lock(mutex);
    hooks[point].push_back(std::move(callback));
}

void HookRunner::run(HookPoint point, HookContext &ctx) const
{
    // copy callbacks under lock, then invoke outside to avoid holding mutex during arbitrary code
    std::vector<HookCallback> callbacks;

    {
        std::lock_guard<std::mutex> lock(mutex);

        auto it = hooks.find(point);

        if (it == hooks.end())
        {
            return;
        }

        callbacks = it->second;
    }

    for (const auto &callback : callbacks)
    {
        try
        {
            callback(ctx);
        }
        catch (const std::exception &e)
        {
            spdlog::error("[HookRunner] Hook failed at point {}: {}", static_cast<int>(point), e.what());
        }
    }
}

} // namespace agent
} // namespace ionclaw
