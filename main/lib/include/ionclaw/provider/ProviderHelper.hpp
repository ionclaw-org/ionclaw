#pragma once

#include <string>

#include "nlohmann/json.hpp"

namespace ionclaw
{
namespace provider
{

class ProviderHelper
{
public:
    // strip "provider/" prefix from model strings
    static std::string stripProviderPrefix(const std::string &model);

    // sanitize tool call ID to only [a-zA-Z0-9_-], with UUID fallback
    static std::string sanitizeToolCallId(const std::string &id, const std::string &prefix = "call_");

    // repair malformed JSON tool arguments (close unclosed braces/brackets/strings)
    static nlohmann::json repairJsonArgs(const std::string &args);

    // redact API keys and tokens from error messages
    static std::string sanitizeErrorMessage(const std::string &msg);

    // classify error type from message text
    static std::string classifyError(const std::string &msg);

    // sanitize tool_calls in assistant messages: drop entries with missing id/name
    static void sanitizeToolCallInputs(nlohmann::json &messages);

    // validate and sanitize tool call name: enforce [A-Za-z0-9_-], max 64 chars
    static std::string sanitizeToolCallName(const std::string &name);
};

} // namespace provider
} // namespace ionclaw
