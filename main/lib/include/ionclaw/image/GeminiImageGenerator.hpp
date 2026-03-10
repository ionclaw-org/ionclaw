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
    std::string generateContent(const std::string &prompt, const std::string &filename,
                                const nlohmann::json &params,
                                const ImageGeneratorContext &context,
                                const std::string &apiKey, const std::string &baseUrl,
                                const std::string &modelId) const;

    std::string predict(const std::string &prompt, const std::string &filename,
                        const nlohmann::json &params,
                        const ImageGeneratorContext &context,
                        const std::string &apiKey, const std::string &baseUrl,
                        const std::string &modelId) const;

    static bool isImagenModel(const std::string &modelId);

    std::string name_;
};

} // namespace image
} // namespace ionclaw
