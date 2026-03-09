#pragma once

#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace ionclaw
{
namespace bus
{

enum class QueueMode
{
    Steer,        // inject into active streaming turn between tool iterations
    Followup,     // enqueue, process as separate turn after current completes
    Collect,      // batch multiple messages into single prompt after debounce
    SteerBacklog, // try steer; if not streaming, fallback to followup
    Interrupt,    // abort current turn, clear queue, process immediately
};

enum class QueueDropPolicy
{
    Old,       // drop oldest items when cap exceeded
    New,       // reject new items when cap exceeded
    Summarize, // drop oldest but keep summary lines
};

// convert QueueMode to string for logging/serialization
std::string queueModeToString(QueueMode mode);

// normalize a string to QueueMode (accepts aliases: "queue" -> Steer, etc.)
std::optional<QueueMode> normalizeQueueMode(const std::string &raw);

// normalize a string to QueueDropPolicy
std::optional<QueueDropPolicy> normalizeQueueDropPolicy(const std::string &raw);

struct InboundMessage
{
    std::string channel;
    std::string senderId;
    std::string chatId;
    std::string content;
    std::vector<std::string> media;
    nlohmann::json metadata;
    std::optional<QueueMode> queueMode; // nullopt = use config default

    std::string sessionKey() const;
};

struct OutboundMessage
{
    std::string channel;
    std::string chatId;
    std::string content;
    nlohmann::json metadata;
};

} // namespace bus
} // namespace ionclaw
