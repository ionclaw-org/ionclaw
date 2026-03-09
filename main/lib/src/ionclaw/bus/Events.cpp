#include "ionclaw/bus/Events.hpp"

#include <algorithm>

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
        return "steer-backlog";
    case QueueMode::Interrupt:
        return "interrupt";
    }

    return "collect";
}

std::optional<QueueMode> normalizeQueueMode(const std::string &raw)
{
    std::string s = raw;
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c)
                   { return c < 0x80 ? static_cast<unsigned char>(std::tolower(c)) : c; });

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

    if (s == "queue" || s == "queued")
    {
        return QueueMode::Steer;
    }

    if (s == "followup" || s == "follow-up")
    {
        return QueueMode::Followup;
    }

    if (s == "collect" || s == "coalesce" || s == "batch")
    {
        return QueueMode::Collect;
    }

    if (s == "steer-backlog" || s == "steer+backlog" || s == "steer_backlog")
    {
        return QueueMode::SteerBacklog;
    }

    if (s == "interrupt" || s == "interrupts" || s == "abort")
    {
        return QueueMode::Interrupt;
    }

    return std::nullopt;
}

std::optional<QueueDropPolicy> normalizeQueueDropPolicy(const std::string &raw)
{
    std::string s = raw;
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c)
                   { return c < 0x80 ? static_cast<unsigned char>(std::tolower(c)) : c; });

    if (s == "old" || s == "oldest")
    {
        return QueueDropPolicy::Old;
    }

    if (s == "new" || s == "newest" || s == "reject")
    {
        return QueueDropPolicy::New;
    }

    if (s == "summarize" || s == "summary")
    {
        return QueueDropPolicy::Summarize;
    }

    return std::nullopt;
}

} // namespace bus
} // namespace ionclaw
