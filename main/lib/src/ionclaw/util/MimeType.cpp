#include "ionclaw/util/MimeType.hpp"

namespace ionclaw
{
namespace util
{

// return mime type for a file path based on extension
std::string MimeType::forPath(const std::string &path)
{
    if (path.size() >= 4)
    {
        std::string ext = path.substr(path.size() - 4);

        if (ext == ".png")
        {
            return "image/png";
        }

        if (ext == ".gif")
        {
            return "image/gif";
        }

        if (ext == ".webp")
        {
            return "image/webp";
        }

        if (ext == ".jpg")
        {
            return "image/jpeg";
        }
    }

    if (path.size() >= 5 && path.substr(path.size() - 5) == ".jpeg")
    {
        return "image/jpeg";
    }

    return "image/png";
}

} // namespace util
} // namespace ionclaw
