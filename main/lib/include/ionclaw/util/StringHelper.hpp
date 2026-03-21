#pragma once

#include <string>
#include <utility>
#include <vector>

namespace ionclaw
{
namespace util
{

class StringHelper
{
public:
    static std::string utf8SafeTruncate(const std::string &str, size_t maxBytes);

    // strip Unicode control, format, line/paragraph separator characters for safe prompt embedding
    static std::string sanitizeForPrompt(const std::string &str);

    // strip reasoning XML tags (<think>, <thinking>, <thought>, <antthinking>) and their content,
    // strip <final> tags but preserve their content (code-aware: preserves tags inside code blocks)
    static std::string stripReasoningTags(const std::string &str);

    // ASCII-safe lowercase: converts ASCII chars to lowercase, leaves non-ASCII bytes unchanged
    static void toLowerInPlace(std::string &str);
    static std::string toLower(const std::string &str);

    // percent-encode a string for use in URL query parameters (RFC 3986)
    static std::string urlEncode(const std::string &str);

    // redact sensitive tokens (API keys, passwords, bearer tokens) in tool output
    static std::string redactSensitive(const std::string &text);

private:
    // mask a token preserving prefix and suffix for identification
    static std::string maskToken(const std::string &token);

    // find code regions (``` fenced blocks and `inline` backticks) in text
    static std::vector<std::pair<size_t, size_t>> findCodeRegions(const std::string &str);

    // check if a position falls inside any code region
    static bool isInsideCode(const std::vector<std::pair<size_t, size_t>> &regions, size_t pos);
};

} // namespace util
} // namespace ionclaw
