#include "ionclaw/bus/Events.hpp"

#include <algorithm>

#include "ionclaw/util/StringHelper.hpp"

namespace ionclaw
{
namespace bus
{

std::string InboundMessage::sessionKey() const
{
    return channel + ":" + chatId;
}

std::string queueModeToString(QueueMode mode)
{
    switch (mode)
    {
    case QueueMode::Steer:
        return "steer";
    case QueueMode::Followup:
        return "followup";
    case QueueMode::Collect:
        return "collect";
    case QueueMode::SteerBacklog:
        return "steer_backlog";
    case QueueMode::Interrupt:
        return "interrupt";
    }

    return "collect";
}

std::optional<QueueMode> normalizeQueueMode(const std::string &raw)
{
    std::string s = raw;
    ionclaw::util::StringHelper::toLowerInPlace(s);

    // trim whitespace
    while (!s.empty() && s.front() == ' ')
    {
        s.erase(s.begin());
    }

    while (!s.empty() && s.back() == ' ')
    {
        s.pop_back();
    }

    if (s.empty())
    {
        return std::nullopt;
    }

    if (s == "steer")
    {
        return QueueMode::Steer;
    }

    if (s == "followup")
    {
        return QueueMode::Followup;
    }

    if (s == "collect")
    {
        return QueueMode::Collect;
    }

    if (s == "steer_backlog")
    {
        return QueueMode::SteerBacklog;
    }

    if (s == "interrupt")
    {
        return QueueMode::Interrupt;
    }

    return std::nullopt;
}

std::optional<QueueDropPolicy> normalizeQueueDropPolicy(const std::string &raw)
{
    std::string s = raw;
    ionclaw::util::StringHelper::toLowerInPlace(s);

    if (s == "old")
    {
        return QueueDropPolicy::Old;
    }

    if (s == "new")
    {
        return QueueDropPolicy::New;
    }

    if (s == "summarize")
    {
        return QueueDropPolicy::Summarize;
    }

    return std::nullopt;
}

} // namespace bus
} // namespace ionclaw
