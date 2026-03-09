#include "ionclaw/image/ImageGeneratorHelper.hpp"

#include <filesystem>
#include <fstream>

namespace ionclaw
{
namespace image
{

std::string ImageGeneratorHelper::saveToPublicMedia(const std::string &imageData,
                                                    const std::string &publicPath,
                                                    const std::string &filename,
                                                    const std::string &publicUrl)
{
    namespace fs = std::filesystem;

    std::string mediaDir = publicPath + "/media";
    std::error_code ec;
    fs::create_directories(mediaDir, ec);

    std::string outputPath = mediaDir + "/" + filename;
    std::ofstream outFile(outputPath, std::ios::binary);

    if (!outFile.is_open())
    {
        return "";
    }

    outFile.write(imageData.data(), static_cast<std::streamsize>(imageData.size()));
    outFile.close();

    // return full URL if public_url is configured, otherwise relative path
    if (!publicUrl.empty())
    {
        std::string base = publicUrl;

        if (!base.empty() && base.back() == '/')
        {
            base.pop_back();
        }

        return base + "/public/media/" + filename;
    }

    return "/public/media/" + filename;
}

} // namespace image
} // namespace ionclaw
