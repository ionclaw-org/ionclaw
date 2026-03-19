#include "ionclaw/util/MimeType.hpp"

namespace ionclaw
{
namespace util
{

// return mime type for a file path based on extension
std::string MimeType::forPath(const std::string &path)
{
    // check 5-char extensions first
    if (path.size() >= 5)
    {
        std::string ext5 = path.substr(path.size() - 5);

        if (ext5 == ".jpeg")
        {
            return "image/jpeg";
        }

        if (ext5 == ".webp")
        {
            return "image/webp";
        }

        if (ext5 == ".tiff")
        {
            return "image/tiff";
        }

        if (ext5 == ".avif")
        {
            return "image/avif";
        }
    }

    // check 4-char extensions
    if (path.size() >= 4)
    {
        std::string ext4 = path.substr(path.size() - 4);

        if (ext4 == ".png")
        {
            return "image/png";
        }

        if (ext4 == ".gif")
        {
            return "image/gif";
        }

        if (ext4 == ".jpg")
        {
            return "image/jpeg";
        }

        if (ext4 == ".bmp")
        {
            return "image/bmp";
        }

        if (ext4 == ".svg")
        {
            return "image/svg+xml";
        }

        if (ext4 == ".tif")
        {
            return "image/tiff";
        }

        if (ext4 == ".ico")
        {
            return "image/x-icon";
        }
    }

    return "application/octet-stream";
}

} // namespace util
} // namespace ionclaw
