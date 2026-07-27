#pragma once

#include <string>
#include <vector>

namespace ionclaw
{
namespace channel
{

// a single deliverable unit: standalone text, a media attachment, or a media attachment with a caption
struct DeliveryPart
{
    std::string text;
    std::string mediaPath;
    std::string mediaKind;
};

// splits an agent reply into deliverable parts using inline markers
// [[break]] starts a new message, [[image:path]] / [[audio:path]] / [[video:path]] / [[document:path]] attach media
// [[photo]] maps to image, [[voice]] to audio, [[file]] to document, and [[media]] infers the kind from the extension
class DeliveryParser
{
public:
    static std::vector<DeliveryPart> parse(const std::string &reply);

private:
    static std::string inferKind(const std::string &path);
    static std::string normalizeKind(const std::string &marker, const std::string &path);
    static std::string trim(const std::string &text);
    static std::vector<std::string> splitOnBreaks(const std::string &reply);
};

} // namespace channel
} // namespace ionclaw
