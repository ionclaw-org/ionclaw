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
    static std::string sanitizeForPrompt(const std::string &str);
    static std::string stripReasoningTags(const std::string &str);

    static void toLowerInPlace(std::string &str);
    static std::string toLower(const std::string &str);

    static std::string trim(const std::string &str);
    static std::string unquote(const std::string &str);

    static std::string urlEncode(const std::string &str);
    static std::string redactSensitive(const std::string &text);

private:
    static std::string maskToken(const std::string &token);
    static std::vector<std::pair<size_t, size_t>> findCodeRegions(const std::string &str);
    static bool isInsideCode(const std::vector<std::pair<size_t, size_t>> &regions, size_t pos);
};

} // namespace util
} // namespace ionclaw
