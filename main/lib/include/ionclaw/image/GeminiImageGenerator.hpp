#pragma once

#include "ionclaw/image/ImageGenerator.hpp"

namespace ionclaw
{
namespace image
{

class GeminiImageGenerator : public ImageGenerator
{
public:
    explicit GeminiImageGenerator(std::string providerName);

    std::string providerName() const override;
    std::string generate(const std::string &prompt, const std::string &filename,
                         const nlohmann::json &params,
                         const ImageGeneratorContext &context) const override;

private:
    static std::string extractModelId(const std::string &model);

    std::string name_;
};

} // namespace image
} // namespace ionclaw
