#include "ionclaw/util/StringHelper.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <regex>
#include <sstream>

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
            // ascii: allow printable + tab, newline, carriage return
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

// find all code regions (fenced ``` blocks and inline `backticks`) in text
// returns sorted vector of [start, end) pairs
std::vector<std::pair<size_t, size_t>> StringHelper::findCodeRegions(const std::string &str)
{
    std::vector<std::pair<size_t, size_t>> regions;

    // fenced code blocks: ```...```
    size_t pos = 0;

    while (pos < str.size())
    {
        auto fenceStart = str.find("```", pos);

        if (fenceStart == std::string::npos)
        {
            break;
        }

        // skip to end of opening fence line
        auto fenceEnd = str.find("```", fenceStart + 3);

        if (fenceEnd == std::string::npos)
        {
            // unclosed fence: treat rest as code
            regions.emplace_back(fenceStart, str.size());
            break;
        }

        regions.emplace_back(fenceStart, fenceEnd + 3);
        pos = fenceEnd + 3;
    }

    // inline backticks: `...`
    pos = 0;

    while (pos < str.size())
    {
        auto tickStart = str.find('`', pos);

        if (tickStart == std::string::npos)
        {
            break;
        }

        // skip if inside a fenced block
        bool inFenced = false;

        for (const auto &[s, e] : regions)
        {
            if (tickStart >= s && tickStart < e)
            {
                inFenced = true;
                pos = e;
                break;
            }
        }

        if (inFenced)
        {
            continue;
        }

        auto tickEnd = str.find('`', tickStart + 1);

        if (tickEnd == std::string::npos)
        {
            break;
        }

        regions.emplace_back(tickStart, tickEnd + 1);
        pos = tickEnd + 1;
    }

    std::sort(regions.begin(), regions.end());
    return regions;
}

// check if a position falls inside any code region
bool StringHelper::isInsideCode(const std::vector<std::pair<size_t, size_t>> &regions, size_t pos)
{
    for (const auto &[start, end] : regions)
    {
        if (pos >= start && pos < end)
        {
            return true;
        }

        if (start > pos)
        {
            break;
        }
    }

    return false;
}

// strip reasoning XML tags from LLM output (code-aware):
// - <think>, <thinking>, <thought>, <antthinking>: remove tags AND content
// - <final>: remove tags but preserve content
// - tags inside code blocks (``` or `) are preserved
std::string StringHelper::stripReasoningTags(const std::string &str)
{
    if (str.empty())
    {
        return str;
    }

    // quick check: skip if no angle brackets present
    if (str.find('<') == std::string::npos)
    {
        return str;
    }

    auto codeRegions = findCodeRegions(str);
    std::string result = str;

    // process thinking tags: remove matched pairs and their content
    static thread_local const std::regex thinkingRe(
        R"(<(?:think|thinking|thought|antthinking)(?:\s[^>]*)?>[\s\S]*?</(?:think|thinking|thought|antthinking)>)",
        std::regex::icase);

    // iteratively remove non-code thinking blocks (positions shift after each removal)
    for (;;)
    {
        auto regions = findCodeRegions(result);
        std::smatch match;

        if (!std::regex_search(result, match, thinkingRe))
        {
            break;
        }

        if (isInsideCode(regions, static_cast<size_t>(match.position())))
        {
            // tag is inside code block - stop (can't skip and continue easily with std::regex)
            break;
        }

        result = result.substr(0, static_cast<size_t>(match.position())) +
                 result.substr(static_cast<size_t>(match.position()) + match.length());
    }

    // remove unclosed thinking tags (only if not inside code)
    static thread_local const std::regex unclosedRe(
        R"(<(?:think|thinking|thought|antthinking)(?:\s[^>]*)?>[\s\S]*$)",
        std::regex::icase);

    {
        auto regions = findCodeRegions(result);
        std::smatch match;

        if (std::regex_search(result, match, unclosedRe) &&
            !isInsideCode(regions, static_cast<size_t>(match.position())))
        {
            result = result.substr(0, static_cast<size_t>(match.position()));
        }
    }

    // strip <final> tags but preserve content (only outside code)
    static thread_local const std::regex finalOpenRe(R"(<final(?:\s[^>]*)?>)", std::regex::icase);
    static thread_local const std::regex finalCloseRe(R"(</final>)", std::regex::icase);

    // simple global replace is safe for <final> tags since they don't remove content
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

void StringHelper::toLowerInPlace(std::string &str)
{
    std::transform(str.begin(), str.end(), str.begin(),
                   [](unsigned char c)
                   { return c < 0x80 ? static_cast<unsigned char>(std::tolower(c)) : c; });
}

std::string StringHelper::toLower(const std::string &str)
{
    std::string result = str;
    toLowerInPlace(result);
    return result;
}

std::string StringHelper::urlEncode(const std::string &str)
{
    std::ostringstream encoded;

    for (char c : str)
    {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            encoded << c;
        }
        else if (c == ' ')
        {
            encoded << '+';
        }
        else
        {
            encoded << '%' << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                    << static_cast<int>(static_cast<unsigned char>(c));
        }
    }

    return encoded.str();
}

// mask a token preserving prefix and suffix for identification
std::string StringHelper::maskToken(const std::string &token)
{
    if (token.size() < 12)
    {
        return "***";
    }

    if (token.size() < 20)
    {
        return token.substr(0, 4) + "..." + token.substr(token.size() - 4);
    }

    return token.substr(0, 6) + "..." + token.substr(token.size() - 4);
}

// redact sensitive tokens in tool output
std::string StringHelper::redactSensitive(const std::string &text)
{
    if (text.empty())
    {
        return text;
    }

    std::string result = text;

    // clang-format off
    static thread_local const std::vector<std::pair<std::regex, int>> patterns = {
        // api key prefixes: sk-, key-, gsk_, ghp_, github_pat_, xox, xapp-, pplx-, npm_, AIza
        {std::regex(R"(\b(sk-[A-Za-z0-9_\-]{10,})\b)"), 1},
        {std::regex(R"(\b(key-[A-Za-z0-9_\-]{10,})\b)"), 1},
        {std::regex(R"(\b(ghp_[A-Za-z0-9]{20,})\b)"), 1},
        {std::regex(R"(\b(github_pat_[A-Za-z0-9_]{20,})\b)"), 1},
        {std::regex(R"(\b(gsk_[A-Za-z0-9_\-]{10,})\b)"), 1},
        {std::regex(R"(\b(xox[baprs]-[A-Za-z0-9\-]{10,})\b)"), 1},
        {std::regex(R"(\b(xapp-[A-Za-z0-9\-]{10,})\b)"), 1},
        {std::regex(R"(\b(AIza[0-9A-Za-z\-_]{20,})\b)"), 1},
        {std::regex(R"(\b(pplx-[A-Za-z0-9_\-]{10,})\b)"), 1},
        {std::regex(R"(\b(npm_[A-Za-z0-9]{10,})\b)"), 1},
        // bearer tokens in headers or assignments
        {std::regex(R"(Bearer\s+([A-Za-z0-9._\-+=]{18,}))"), 1},
        // env-style assignments: KEY=value, TOKEN="value"
        {std::regex(R"re(\b[A-Z0-9_]*(?:KEY|TOKEN|SECRET|PASSWORD|PASSWD)\s*[=:]\s*["']?([^\s"']{8,})["']?)re"), 1},
        // json fields: "apiKey": "value", "token": "value"
        {std::regex(R"re("(?:apiKey|api_key|token|secret|password|passwd|access_token|refresh_token)"\s*:\s*"([^"]{8,})")re"), 1},
        // telegram bot tokens
        {std::regex(R"(\b(\d{8,}:[A-Za-z0-9_\-]{20,})\b)"), 1},
    };
    // clang-format on

    for (const auto &[pattern, group] : patterns)
    {
        std::string processed;
        auto begin = std::sregex_iterator(result.begin(), result.end(), pattern);
        auto end = std::sregex_iterator();
        size_t lastPos = 0;

        for (auto it = begin; it != end; ++it)
        {
            auto &match = *it;
            processed += result.substr(lastPos, static_cast<size_t>(match.position()) - lastPos);

            // replace only the captured group within the full match
            auto fullMatch = match.str();
            auto captured = match.str(group);
            auto capturedPos = fullMatch.find(captured);

            if (capturedPos != std::string::npos)
            {
                processed += fullMatch.substr(0, capturedPos);
                processed += maskToken(captured);
                processed += fullMatch.substr(capturedPos + captured.size());
            }

            lastPos = static_cast<size_t>(match.position()) + fullMatch.size();
        }

        processed += result.substr(lastPos);
        result = std::move(processed);
    }

    // pem private keys
    static thread_local const std::regex pemPattern(R"(-----BEGIN [A-Z ]*PRIVATE KEY-----[\s\S]+?-----END [A-Z ]*PRIVATE KEY-----)");
    result = std::regex_replace(result, pemPattern, "[REDACTED PRIVATE KEY]");

    return result;
}

} // namespace util
} // namespace ionclaw
