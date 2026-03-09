#pragma once

#include "ionclaw/image/ImageGenerator.hpp"

namespace ionclaw
{
namespace image
{

class LocalImageGenerator : public ImageGenerator
{
public:
    std::string providerName() const override;
    std::string generate(const std::string &prompt, const std::string &filename,
                         const nlohmann::json &params,
                         const ImageGeneratorContext &context) const override;
};

} // namespace image
} // namespace ionclaw
