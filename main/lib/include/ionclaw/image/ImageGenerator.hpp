#pragma once

#include <string>

#include "nlohmann/json.hpp"

namespace ionclaw
{
namespace config
{
struct Config;
}

namespace image
{

struct ImageGeneratorContext
{
    std::string workspacePath;
    std::string publicPath;
    std::string projectPath;
    std::string model;
    std::string providerName;
    const ionclaw::config::Config *config = nullptr;
};

class ImageGenerator
{
public:
    virtual ~ImageGenerator() = default;

    virtual std::string providerName() const = 0;
    virtual std::string generate(const std::string &prompt, const std::string &filename,
                                 const nlohmann::json &params,
                                 const ImageGeneratorContext &context) const = 0;
};

} // namespace image
} // namespace ionclaw
