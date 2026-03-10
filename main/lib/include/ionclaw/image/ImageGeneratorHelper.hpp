#pragma once

#include <string>
#include <vector>

#include "nlohmann/json.hpp"

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

    // strip [media: path (type)] annotation format, returning just the path
    static std::string cleanReferencePath(const std::string &raw);

    // strip provider prefix from model id (e.g. "openai/gpt-image-1" → "gpt-image-1")
    static std::string extractModelId(const std::string &model);

    // resolve reference image paths from params, cleaning annotations
    static std::vector<std::string> resolveReferencePaths(const nlohmann::json &params,
                                                          const std::string &workspacePath,
                                                          const std::string &publicPath);

    // parse OpenAI-style response: extract b64_json from data[0], decode, save
    static std::string decodeAndSave(const std::string &responseBody,
                                     const std::string &publicPath,
                                     const std::string &filename,
                                     const std::string &publicUrl);
};

} // namespace image
} // namespace ionclaw
