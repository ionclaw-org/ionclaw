#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ionclaw/image/ImageGenerator.hpp"

namespace ionclaw
{
namespace image
{

class ImageGeneratorRegistry
{
public:
    static ImageGeneratorRegistry &instance();

    void registerGenerator(const std::string &name, std::unique_ptr<ImageGenerator> generator);
    ImageGenerator *get(const std::string &name) const;
    std::vector<std::string> providerNames() const;

private:
    ImageGeneratorRegistry();
    std::unordered_map<std::string, std::unique_ptr<ImageGenerator>> generators_;
};

} // namespace image
} // namespace ionclaw
