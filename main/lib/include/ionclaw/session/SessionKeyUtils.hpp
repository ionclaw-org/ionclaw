#pragma once

#include <optional>
#include <string>

namespace ionclaw
{
namespace session
{

struct ParsedSessionKey
{
    std::string agentId;
    std::string channel;
    std::string chatId;
    std::string baseKey; // "channel:chatId"
};

class SessionKeyUtils
{
public:
    // build agent-scoped session key: "agent:{agentId}:{channel}:{chatId}"
    static std::string build(const std::string &agentId, const std::string &channel, const std::string &chatId);

    // build agent-scoped key from base key: "agent:{agentId}:{baseKey}"
    static std::string buildFromBase(const std::string &agentId, const std::string &baseKey);

    // parse an agent-scoped key into components; returns nullopt if format is invalid
    static std::optional<ParsedSessionKey> parse(const std::string &key);

    // extract channel from either format ("agent:x:web:y" -> "web", "web:y" -> "web")
    static std::string extractChannel(const std::string &key);

    // strip agent prefix if present ("agent:x:web:y" -> "web:y", "web:y" -> "web:y")
    static std::string extractBaseKey(const std::string &key);

    // extract chatId from either format ("agent:x:web:y" -> "y", "web:y" -> "y")
    static std::string extractChatId(const std::string &key);

    // check if key has agent prefix
    static bool isAgentScoped(const std::string &key);

    static constexpr const char *AGENT_PREFIX = "agent:";
};

} // namespace session
} // namespace ionclaw
