#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace ionclaw
{
namespace embedding
{

class TextChunker
{
public:
    static std::vector<std::string> chunk(const std::string &text, size_t maxChars, size_t overlapChars);

private:
    static size_t utf8SafeBoundary(const std::string &text, size_t pos);
    static std::string trim(const std::string &text);
};

} // namespace embedding
} // namespace ionclaw
