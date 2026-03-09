#pragma once

#include <string>

namespace ionclaw
{
namespace image
{

class ImageGeneratorHelper
{
public:
    static std::string saveToPublicMedia(const std::string &imageData,
                                         const std::string &publicPath,
                                         const std::string &filename,
                                         const std::string &publicUrl);
};

} // namespace image
} // namespace ionclaw
