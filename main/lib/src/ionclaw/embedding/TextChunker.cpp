#include "ionclaw/embedding/TextChunker.hpp"

#include <algorithm>
#include <cctype>

namespace ionclaw
{
namespace embedding
{

size_t TextChunker::utf8SafeBoundary(const std::string &text, size_t pos)
{
    // a utf-8 continuation byte carries the 10xxxxxx bit pattern, so back up until a leading byte is reached
    while (pos > 0 && pos < text.size() && (static_cast<unsigned char>(text[pos]) & 0xC0) == 0x80)
    {
        --pos;
    }

    return pos;
}

std::string TextChunker::trim(const std::string &text)
{
    size_t start = 0;
    size_t end = text.size();

    while (start < end && std::isspace(static_cast<unsigned char>(text[start])))
    {
        ++start;
    }

    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])))
    {
        --end;
    }

    return text.substr(start, end - start);
}

std::vector<std::string> TextChunker::chunk(const std::string &text, size_t maxChars, size_t overlapChars)
{
    std::vector<std::string> chunks;

    if (maxChars == 0 || overlapChars >= maxChars)
    {
        return chunks;
    }

    auto trimmed = trim(text);

    if (trimmed.empty())
    {
        return chunks;
    }

    if (trimmed.size() <= maxChars)
    {
        chunks.push_back(trimmed);
        return chunks;
    }

    size_t pos = 0;

    while (pos < trimmed.size())
    {
        size_t end = utf8SafeBoundary(trimmed, std::min(pos + maxChars, trimmed.size()));

        // prefer to cut on whitespace within the last part of the window so a word is not split in half
        if (end < trimmed.size())
        {
            size_t minBreak = pos + (maxChars * 3) / 4;
            auto ws = trimmed.find_last_of(" \n\t", end);

            if (ws != std::string::npos && ws > minBreak)
            {
                end = ws;
            }
        }

        auto piece = trim(trimmed.substr(pos, end - pos));

        if (!piece.empty())
        {
            chunks.push_back(piece);
        }

        if (end >= trimmed.size())
        {
            break;
        }

        // advance by the window minus the overlap so consecutive chunks share context
        size_t advance = std::max<size_t>(1, (end - pos) - overlapChars);
        size_t next = utf8SafeBoundary(trimmed, pos + advance);

        // fall back to the chunk boundary when a large overlap on multibyte text would otherwise stall
        pos = next > pos ? next : end;
    }

    return chunks;
}

} // namespace embedding
} // namespace ionclaw
