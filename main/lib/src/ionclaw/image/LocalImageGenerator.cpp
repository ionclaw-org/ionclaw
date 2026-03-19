#include "ionclaw/image/LocalImageGenerator.hpp"

#include <filesystem>
#include <vector>

#ifdef IONCLAW_HAS_STB_IMAGE_WRITE
#include "stb_image_write.h"
#endif

namespace ionclaw
{
namespace image
{

std::string LocalImageGenerator::providerName() const
{
    return "local";
}

std::string LocalImageGenerator::generate(const std::string &prompt,
                                          const std::string &filename,
                                          const nlohmann::json &params,
                                          const ImageGeneratorContext &context) const
{
    (void)prompt;

#ifdef IONCLAW_HAS_STB_IMAGE_WRITE
    // size from params or default 512x512
    int width = 512;
    int height = 512;

    if (params.contains("size") && params["size"].is_string())
    {
        std::string s = params["size"].get<std::string>();

        if (s == "1K")
        {
            width = height = 1024;
        }
        else if (s == "2K")
        {
            width = height = 2048;
        }
        else if (s == "4K")
        {
            width = height = 4096;
        }
    }

    // gradient rgb buffer (placeholder image)
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 3);

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            size_t i = static_cast<size_t>((y * width + x) * 3);
            pixels[i + 0] = static_cast<unsigned char>((x * 255) / (width > 1 ? width : 1));
            pixels[i + 1] = static_cast<unsigned char>((y * 255) / (height > 1 ? height : 1));
            pixels[i + 2] = 128;
        }
    }

    // write to public/media directory
    std::string mediaDir = context.publicPath + "/media";
    std::error_code ec;
    std::filesystem::create_directories(mediaDir, ec);

    if (ec)
    {
        return "Error: failed to create media directory: " + ec.message();
    }

    std::string path = mediaDir + "/" + filename;

    if (stbi_write_png(path.c_str(), width, height, 3, pixels.data(), 0) == 0)
    {
        return "Error: failed to write local image";
    }

    return "Image saved: public/media/" + filename;
#else
    (void)filename;
    (void)params;
    (void)context;
    return "Error: local image generation requires stb_image_write. Set image.model to gemini/... or openai/... for API-based generation.";
#endif
}

} // namespace image
} // namespace ionclaw
