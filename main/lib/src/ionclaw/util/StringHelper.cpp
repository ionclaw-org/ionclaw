#include "ionclaw/util/StringHelper.hpp"

#include <algorithm>
#include <regex>

namespace ionclaw
{
namespace util
{

// truncate a utf-8 string to at most maxBytes without breaking multi-byte sequences
std::string StringHelper::utf8SafeTruncate(const std::string &str, size_t maxBytes)
{
    if (str.size() <= maxBytes)
    {
        return str;
    }

    // walk backward from cut point to find valid code-point boundary
    size_t pos = maxBytes;

    while (pos > 0 && (static_cast<unsigned char>(str[pos]) & 0xC0) == 0x80)
    {
        --pos;
    }

    // verify lead byte's expected sequence length fits within maxBytes
    if (pos > 0 || (static_cast<unsigned char>(str[0]) & 0x80) != 0)
    {
        auto lead = static_cast<unsigned char>(str[pos]);
        size_t seqLen = 1;

        if ((lead & 0xE0) == 0xC0)
        {
            seqLen = 2;
        }
        else if ((lead & 0xF0) == 0xE0)
        {
            seqLen = 3;
        }
        else if ((lead & 0xF8) == 0xF0)
        {
            seqLen = 4;
        }

        if (pos + seqLen > maxBytes)
        {
            return str.substr(0, pos);
        }
    }

    return str.substr(0, maxBytes);
}

// strip Unicode control (Cc), format (Cf), line separator (U+2028), paragraph separator (U+2029)
// from strings to be embedded in prompts, preventing injection via invisible characters
std::string StringHelper::sanitizeForPrompt(const std::string &str)
{
    std::string result;
    result.reserve(str.size());

    size_t i = 0;

    while (i < str.size())
    {
        auto byte = static_cast<unsigned char>(str[i]);

        if (byte < 0x80)
        {
            // ASCII: allow printable + tab, newline, carriage return
            if (byte >= 0x20 || byte == '\t' || byte == '\n' || byte == '\r')
            {
                result += str[i];
            }

            // silently drop other ASCII control chars (0x00-0x1F except \t\n\r, 0x7F)
            ++i;
        }
        else if ((byte & 0xE0) == 0xC0 && i + 1 < str.size())
        {
            // 2-byte sequence: validate continuation byte
            auto b2 = static_cast<unsigned char>(str[i + 1]);

            if ((b2 & 0xC0) != 0x80)
            {
                ++i; // invalid continuation, skip lead byte
                continue;
            }

            auto cp = ((byte & 0x1F) << 6) | (b2 & 0x3F);

            // skip C1 controls (U+0080..U+009F) and soft hyphen (U+00AD, Cf)
            if (cp >= 0x00A0 && cp != 0x00AD)
            {
                result += str[i];
                result += str[i + 1];
            }

            i += 2;
        }
        else if ((byte & 0xF0) == 0xE0 && i + 2 < str.size())
        {
            // 3-byte sequence: validate continuation bytes
            auto b2 = static_cast<unsigned char>(str[i + 1]);
            auto b3 = static_cast<unsigned char>(str[i + 2]);

            if ((b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80)
            {
                ++i; // invalid continuation, skip lead byte
                continue;
            }

            auto cp = ((byte & 0x0F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F);

            // skip line separator (U+2028), paragraph separator (U+2029)
            // skip zero-width chars (U+200B-U+200F, U+202A-U+202E, U+2060-U+2064, U+FEFF)
            bool skip = (cp == 0x2028 || cp == 0x2029 ||
                         (cp >= 0x200B && cp <= 0x200F) ||
                         (cp >= 0x202A && cp <= 0x202E) ||
                         (cp >= 0x2060 && cp <= 0x2064) ||
                         cp == 0xFEFF);

            if (!skip)
            {
                result += str[i];
                result += str[i + 1];
                result += str[i + 2];
            }

            i += 3;
        }
        else if ((byte & 0xF8) == 0xF0 && i + 3 < str.size())
        {
            // 4-byte sequence: validate continuation bytes
            auto b2 = static_cast<unsigned char>(str[i + 1]);
            auto b3 = static_cast<unsigned char>(str[i + 2]);
            auto b4 = static_cast<unsigned char>(str[i + 3]);

            if ((b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80 || (b4 & 0xC0) != 0x80)
            {
                ++i; // invalid continuation, skip lead byte
                continue;
            }

            result += str[i];
            result += str[i + 1];
            result += str[i + 2];
            result += str[i + 3];
            i += 4;
        }
        else
        {
            // invalid UTF-8 byte: skip
            ++i;
        }
    }

    return result;
}

// strip reasoning XML tags from LLM output:
// - <think>, <thinking>, <thought>, <antthinking>: remove tags AND content
// - <final>: remove tags but preserve content
std::string StringHelper::stripReasoningTags(const std::string &str)
{
    if (str.empty())
    {
        return str;
    }

    // quick check: skip regex if no angle brackets present
    if (str.find('<') == std::string::npos)
    {
        return str;
    }

    std::string result = str;

    // remove thinking tags and their content (case-insensitive)
    // matches: <think>...</think>, <thinking>...</thinking>, <thought>...</thought>, <antthinking>...</antthinking>
    static thread_local const std::regex thinkingRe(
        R"(<(?:think|thinking|thought|antthinking)(?:\s[^>]*)?>[\s\S]*?</(?:think|thinking|thought|antthinking)>)",
        std::regex::icase);
    result = std::regex_replace(result, thinkingRe, "");

    // remove unclosed thinking tags and trailing content (strict mode)
    static thread_local const std::regex unclosedThinkingRe(
        R"(<(?:think|thinking|thought|antthinking)(?:\s[^>]*)?>[\s\S]*$)",
        std::regex::icase);
    result = std::regex_replace(result, unclosedThinkingRe, "");

    // strip <final> tags but preserve their content
    static thread_local const std::regex finalOpenRe(R"(<final(?:\s[^>]*)?>)", std::regex::icase);
    static thread_local const std::regex finalCloseRe(R"(</final>)", std::regex::icase);
    result = std::regex_replace(result, finalOpenRe, "");
    result = std::regex_replace(result, finalCloseRe, "");

    // trim leading/trailing whitespace
    auto start = result.find_first_not_of(" \t\n\r");
    auto end = result.find_last_not_of(" \t\n\r");

    if (start == std::string::npos || end == std::string::npos)
    {
        return "";
    }

    return result.substr(start, end - start + 1);
}

} // namespace util
} // namespace ionclaw
