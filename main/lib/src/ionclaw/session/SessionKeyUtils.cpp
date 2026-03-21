#include "ionclaw/session/SessionKeyUtils.hpp"

namespace ionclaw
{
namespace session
{

std::string SessionKeyUtils::build(const std::string &agentId, const std::string &channel, const std::string &chatId)
{
    return std::string(AGENT_PREFIX) + agentId + ":" + channel + ":" + chatId;
}

std::string SessionKeyUtils::buildFromBase(const std::string &agentId, const std::string &baseKey)
{
    return std::string(AGENT_PREFIX) + agentId + ":" + baseKey;
}

std::optional<ParsedSessionKey> SessionKeyUtils::parse(const std::string &key)
{
    // expected format: "agent:{agentId}:{channel}:{chatId}"
    if (!isAgentScoped(key))
    {
        return std::nullopt;
    }

    // skip "agent:" prefix
    auto rest = key.substr(6);

    // find agentId (first segment after "agent:")
    auto firstColon = rest.find(':');

    if (firstColon == std::string::npos)
    {
        return std::nullopt;
    }

    auto agentId = rest.substr(0, firstColon);
    auto baseKey = rest.substr(firstColon + 1);

    // find channel (first segment of baseKey)
    auto secondColon = baseKey.find(':');

    if (secondColon == std::string::npos)
    {
        return std::nullopt;
    }

    ParsedSessionKey parsed;
    parsed.agentId = agentId;
    parsed.channel = baseKey.substr(0, secondColon);
    parsed.chatId = baseKey.substr(secondColon + 1);
    parsed.baseKey = baseKey;
    return parsed;
}

std::string SessionKeyUtils::extractChannel(const std::string &key)
{
    if (isAgentScoped(key))
    {
        auto parsed = parse(key);
        return parsed ? parsed->channel : "";
    }

    // base key format: "channel:chatId"
    auto colonPos = key.find(':');
    return (colonPos != std::string::npos) ? key.substr(0, colonPos) : key;
}

std::string SessionKeyUtils::extractBaseKey(const std::string &key)
{
    if (!isAgentScoped(key))
    {
        return key;
    }

    // skip "agent:{agentId}:" to get base key
    auto rest = key.substr(6);
    auto colonPos = rest.find(':');

    if (colonPos == std::string::npos)
    {
        return key;
    }

    return rest.substr(colonPos + 1);
}

std::string SessionKeyUtils::extractChatId(const std::string &key)
{
    if (isAgentScoped(key))
    {
        auto parsed = parse(key);
        return parsed ? parsed->chatId : "";
    }

    // base key format: "channel:chatId"
    auto colonPos = key.find(':');
    return (colonPos != std::string::npos) ? key.substr(colonPos + 1) : key;
}

bool SessionKeyUtils::isAgentScoped(const std::string &key)
{
    return key.size() > 6 && key.rfind(AGENT_PREFIX, 0) == 0;
}

} // namespace session
} // namespace ionclaw
