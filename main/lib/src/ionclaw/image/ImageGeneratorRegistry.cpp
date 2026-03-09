#include "ionclaw/image/ImageGeneratorRegistry.hpp"

#include "ionclaw/image/GeminiImageGenerator.hpp"
#include "ionclaw/image/OpenAIStyleImageGenerator.hpp"

namespace ionclaw
{
namespace image
{

ImageGeneratorRegistry::ImageGeneratorRegistry()
{
    registerGenerator("gemini", std::make_unique<GeminiImageGenerator>("gemini"));
    registerGenerator("google", std::make_unique<GeminiImageGenerator>("google"));
    registerGenerator("openai", std::make_unique<OpenAIStyleImageGenerator>("openai"));
    registerGenerator("grok", std::make_unique<OpenAIStyleImageGenerator>("grok"));
}

ImageGeneratorRegistry &ImageGeneratorRegistry::instance()
{
    static ImageGeneratorRegistry inst;
    return inst;
}

void ImageGeneratorRegistry::registerGenerator(const std::string &name,
                                               std::unique_ptr<ImageGenerator> generator)
{
    if (generator)
    {
        generators_[name] = std::move(generator);
    }
}

ImageGenerator *ImageGeneratorRegistry::get(const std::string &name) const
{
    auto it = generators_.find(name);

    if (it != generators_.end())
    {
        return it->second.get();
    }

    return nullptr;
}

std::vector<std::string> ImageGeneratorRegistry::providerNames() const
{
    std::vector<std::string> names;
    names.reserve(generators_.size());

    for (const auto &p : generators_)
    {
        names.push_back(p.first);
    }

    return names;
}

} // namespace image
} // namespace ionclaw
