#pragma once

#include "ionclaw/image/ImageGenerator.hpp"

namespace ionclaw
{
namespace image
{

class GrokImageGenerator : public ImageGenerator
{
public:
    explicit GrokImageGenerator(std::string providerName);

    std::string providerName() const override;
    std::string generate(const std::string &prompt, const std::string &filename,
                         const nlohmann::json &params,
                         const ImageGeneratorContext &context) const override;

private:
    std::string generateImage(const std::string &prompt, const std::string &filename,
                              const nlohmann::json &params,
                              const ImageGeneratorContext &context,
                              const std::string &apiKey, const std::string &baseUrl) const;

    std::string editImage(const std::string &prompt, const std::string &filename,
                          const nlohmann::json &params,
                          const ImageGeneratorContext &context,
                          const std::string &apiKey, const std::string &baseUrl) const;

    std::string name_;
};

} // namespace image
} // namespace ionclaw
