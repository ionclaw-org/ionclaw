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
};

} // namespace util
} // namespace ionclaw
