#include "ionclaw/provider/ProviderHelper.hpp"

#include <algorithm>
#include <regex>

#include "ionclaw/util/StringHelper.hpp"
#include "ionclaw/util/UniqueId.hpp"
#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace provider
{

// strip "provider/" prefix from model strings
std::string ProviderHelper::stripProviderPrefix(const std::string &model)
{
    auto pos = model.find('/');

    if (pos != std::string::npos)
    {
        return model.substr(pos + 1);
    }

    return model;
}

// sanitize tool call ID to only [a-zA-Z0-9_-], with UUID fallback
std::string ProviderHelper::sanitizeToolCallId(const std::string &id, const std::string &prefix)
{
    if (id.empty())
    {
        return prefix + ionclaw::util::UniqueId::uuid().substr(0, 12);
    }

    // check if id already contains only valid characters
    static thread_local std::regex validPattern("^[a-zA-Z0-9_-]+$");

    if (std::regex_match(id, validPattern))
    {
        return id;
    }

    // replace invalid chars
    std::string sanitized;
    sanitized.reserve(id.size());

    for (unsigned char c : id)
    {
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '-')
        {
            sanitized += c;
        }
        else
        {
            sanitized += '_';
        }
    }

    // fallback to uuid if sanitized result is empty
    return sanitized.empty() ? prefix + ionclaw::util::UniqueId::uuid().substr(0, 12) : sanitized;
}

// redact api keys and tokens from error messages
std::string ProviderHelper::sanitizeErrorMessage(const std::string &msg)
{
    // replace known key patterns with redacted placeholder
    std::string safe = msg;
    safe = std::regex_replace(safe, std::regex("sk-[a-zA-Z0-9]{10,}"), "[REDACTED]");
    safe = std::regex_replace(safe, std::regex("key-[a-zA-Z0-9]{10,}"), "[REDACTED]");
    safe = std::regex_replace(safe, std::regex("Bearer\\s+[a-zA-Z0-9_\\-\\.]{10,}"), "Bearer [REDACTED]");
    return safe;
}

// repair malformed JSON tool arguments
nlohmann::json ProviderHelper::repairJsonArgs(const std::string &args)
{
    if (args.empty())
    {
        return nlohmann::json::object();
    }

    // try parsing as-is
    try
    {
        return nlohmann::json::parse(args);
    }
    catch (...)
    {
    }

    // scan for unclosed delimiters
    std::string repaired = args;
    int braces = 0;
    int brackets = 0;
    bool inString = false;
    bool escaped = false;

    for (char c : repaired)
    {
        if (escaped)
        {
            escaped = false;
            continue;
        }

        if (c == '\\')
        {
            escaped = true;
            continue;
        }

        if (c == '"')
        {
            inString = !inString;
            continue;
        }

        if (!inString)
        {
            if (c == '{')
            {
                braces++;
            }
            else if (c == '}')
            {
                braces--;
            }
            else if (c == '[')
            {
                brackets++;
            }
            else if (c == ']')
            {
                brackets--;
            }
        }
    }

    // close unclosed strings
    if (inString)
    {
        repaired += '"';
    }

    // close unclosed brackets
    while (brackets > 0)
    {
        repaired += ']';
        brackets--;
    }

    // close unclosed braces
    while (braces > 0)
    {
        repaired += '}';
        braces--;
    }

    // try parsing repaired string
    try
    {
        return nlohmann::json::parse(repaired);
    }
    catch (...)
    {
        spdlog::warn("[ProviderHelper] failed to repair JSON args: {}", ionclaw::util::StringHelper::utf8SafeTruncate(args, 200));
        return nlohmann::json::object();
    }
}

// classify error type from message text
std::string ProviderHelper::classifyError(const std::string &msg)
{
    // convert to lowercase for case-insensitive matching
    auto lower = msg;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c)
                   { return c < 0x80 ? static_cast<unsigned char>(std::tolower(c)) : c; });

    // context overflow
    for (const auto &p : {"context_length_exceeded", "maximum context length", "too many tokens",
                          "prompt is too long", "request too large", "content_too_large"})
    {
        if (lower.find(p) != std::string::npos)
        {
            return "context_overflow";
        }
    }

    // rate limit
    for (const auto &p : {"rate_limit", "rate limit", "429", "too many requests",
                          "quota exceeded", "overloaded"})
    {
        if (lower.find(p) != std::string::npos)
        {
            return "rate_limit";
        }
    }

    // billing
    for (const auto &p : {"billing", "insufficient_quota", "payment", "402",
                          "exceeded your current quota"})
    {
        if (lower.find(p) != std::string::npos)
        {
            return "billing";
        }
    }

    // auth
    for (const auto &p : {"authentication", "unauthorized", "401", "403",
                          "invalid api key", "invalid_api_key", "permission denied"})
    {
        if (lower.find(p) != std::string::npos)
        {
            return "auth";
        }
    }

    // model not found
    for (const auto &p : {"model_not_found", "model not found", "does not exist"})
    {
        if (lower.find(p) != std::string::npos)
        {
            return "model_not_found";
        }
    }

    // timeout (more specific, before general transient)
    for (const auto &p : {"timeout", "timed out", "deadline exceeded", "request timeout",
                          "read timeout", "connect timeout", "socket timeout",
                          "operation timed out", "etimedout", "esockettimedout",
                          "econnaborted", "abort", "aborted"})
    {
        if (lower.find(p) != std::string::npos)
        {
            return "timeout";
        }
    }

    // role ordering
    for (const auto &p : {"roles must alternate", "incorrect role information",
                          "function call turn comes immediately after",
                          "unexpected role", "invalid turn order"})
    {
        if (lower.find(p) != std::string::npos)
        {
            return "role_ordering";
        }
    }

    // thinking/reasoning constraints
    for (const auto &p : {"reasoning is mandatory", "reasoning is required",
                          "cannot be disabled", "thinking is required",
                          "budget_tokens", "thinking budget"})
    {
        if (lower.find(p) != std::string::npos)
        {
            return "thinking_constraint";
        }
    }

    // transient
    for (const auto &p : {"connection", "503", "502", "500",
                          "internal server error", "bad gateway", "service unavailable",
                          "econnreset", "econnrefused", "enetunreach", "ehostunreach",
                          "epipe", "network error", "fetch failed"})
    {
        if (lower.find(p) != std::string::npos)
        {
            return "transient";
        }
    }

    return "unknown";
}

// sanitize tool_calls in assistant messages: drop entries with missing id or name
void ProviderHelper::sanitizeToolCallInputs(nlohmann::json &messages)
{
    for (auto &msg : messages)
    {
        if (msg.value("role", "") != "assistant" || !msg.contains("tool_calls") || !msg["tool_calls"].is_array())
        {
            continue;
        }

        nlohmann::json cleanCalls = nlohmann::json::array();

        for (const auto &tc : msg["tool_calls"])
        {
            auto id = tc.value("id", "");
            auto name = tc.contains("function") ? tc["function"].value("name", "") : "";

            if (id.empty() || name.empty())
            {
                spdlog::debug("[ProviderHelper] Dropping tool call with missing id or name");
                continue;
            }

            // sanitize tool call name
            auto sanitizedName = sanitizeToolCallName(name);

            if (sanitizedName != name)
            {
                auto fixed = tc;
                fixed["function"]["name"] = sanitizedName;
                cleanCalls.push_back(fixed);
                spdlog::debug("[ProviderHelper] Sanitized tool call name '{}' → '{}'", name, sanitizedName);
                continue;
            }

            cleanCalls.push_back(tc);
        }

        msg["tool_calls"] = cleanCalls;

        // if all tool_calls were removed, remove the key entirely
        if (cleanCalls.empty())
        {
            msg.erase("tool_calls");
        }
    }
}

// validate and sanitize tool call name: enforce [A-Za-z0-9_-], max 64 chars
std::string ProviderHelper::sanitizeToolCallName(const std::string &name)
{
    static constexpr size_t MAX_TOOL_NAME_CHARS = 64;

    std::string sanitized;
    sanitized.reserve(std::min(name.size(), MAX_TOOL_NAME_CHARS));

    for (size_t i = 0; i < name.size() && sanitized.size() < MAX_TOOL_NAME_CHARS; ++i)
    {
        unsigned char c = name[i];

        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '-')
        {
            sanitized += c;
        }
    }

    if (sanitized.empty())
    {
        return "unknown_tool";
    }

    return sanitized;
}

} // namespace provider
} // namespace ionclaw
