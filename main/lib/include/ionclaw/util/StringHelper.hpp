#pragma once

#include <string>

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
    // strip <final> tags but preserve their content
    static std::string stripReasoningTags(const std::string &str);

    // ASCII-safe lowercase: converts ASCII chars to lowercase, leaves non-ASCII bytes unchanged
    static void toLowerInPlace(std::string &str);
    static std::string toLower(const std::string &str);

    // percent-encode a string for use in URL query parameters (RFC 3986)
    static std::string urlEncode(const std::string &str);
};

} // namespace util
} // namespace ionclaw
