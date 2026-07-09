#include "ionclaw/channel/DeliveryParser.hpp"

#include <algorithm>
#include <cctype>

namespace ionclaw
{
namespace channel
{

std::string DeliveryParser::trim(const std::string &text)
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

std::string DeliveryParser::inferKind(const std::string &path)
{
    auto dot = path.find_last_of('.');

    if (dot == std::string::npos)
    {
        return "document";
    }

    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif" || ext == "webp" || ext == "bmp")
    {
        return "image";
    }

    if (ext == "mp3" || ext == "ogg" || ext == "wav" || ext == "m4a" || ext == "opus" || ext == "aac")
    {
        return "audio";
    }

    if (ext == "mp4" || ext == "mov" || ext == "webm" || ext == "mkv" || ext == "avi")
    {
        return "video";
    }

    return "document";
}

std::string DeliveryParser::normalizeKind(const std::string &marker, const std::string &path)
{
    if (marker == "image" || marker == "photo")
    {
        return "image";
    }

    if (marker == "audio" || marker == "voice")
    {
        return "audio";
    }

    if (marker == "video")
    {
        return "video";
    }

    if (marker == "document" || marker == "file")
    {
        return "document";
    }

    // the generic media marker resolves the kind from the file extension
    return inferKind(path);
}

std::vector<std::string> DeliveryParser::splitOnBreaks(const std::string &reply)
{
    static const std::string token = "[[break]]";

    std::vector<std::string> segments;
    size_t pos = 0;

    while (true)
    {
        auto next = reply.find(token, pos);

        if (next == std::string::npos)
        {
            segments.push_back(reply.substr(pos));
            break;
        }

        segments.push_back(reply.substr(pos, next - pos));
        pos = next + token.size();
    }

    return segments;
}

std::vector<DeliveryPart> DeliveryParser::parse(const std::string &reply)
{
    std::vector<DeliveryPart> parts;

    for (const auto &segment : splitOnBreaks(reply))
    {
        std::vector<DeliveryPart> media;
        std::string residual;
        size_t pos = 0;

        // scan the segment for [[kind:path]] markers, collecting media and the surrounding text
        while (pos < segment.size())
        {
            auto open = segment.find("[[", pos);

            if (open == std::string::npos)
            {
                residual += segment.substr(pos);
                break;
            }

            auto close = segment.find("]]", open + 2);

            if (close == std::string::npos)
            {
                residual += segment.substr(pos);
                break;
            }

            auto inner = segment.substr(open + 2, close - (open + 2));
            auto colon = inner.find(':');

            std::string marker = colon == std::string::npos ? inner : inner.substr(0, colon);
            std::string path = colon == std::string::npos ? "" : trim(inner.substr(colon + 1));

            bool isMediaMarker = (marker == "image" || marker == "photo" || marker == "audio" || marker == "voice" || marker == "video" || marker == "document" || marker == "file" || marker == "media") && !path.empty();

            if (isMediaMarker)
            {
                // text before the marker stays in the residual, the marker itself becomes a media part
                residual += segment.substr(pos, open - pos);

                DeliveryPart part;
                part.mediaPath = path;
                part.mediaKind = normalizeKind(marker, path);
                media.push_back(part);
            }
            else
            {
                // not a delivery marker, keep it verbatim
                residual += segment.substr(pos, close + 2 - pos);
            }

            pos = close + 2;
        }

        auto text = trim(residual);

        if (media.empty())
        {
            if (!text.empty())
            {
                DeliveryPart part;
                part.text = text;
                parts.push_back(part);
            }

            continue;
        }

        // the residual text becomes the caption of the first media part in the segment
        media.front().text = text;

        for (auto &part : media)
        {
            parts.push_back(part);
        }
    }

    return parts;
}

} // namespace channel
} // namespace ionclaw
