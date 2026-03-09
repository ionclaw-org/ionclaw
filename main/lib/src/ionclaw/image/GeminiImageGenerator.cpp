#include "ionclaw/image/GeminiImageGenerator.hpp"

#include "ionclaw/config/Config.hpp"
#include "ionclaw/image/ImageGeneratorHelper.hpp"
#include "ionclaw/tool/builtin/ToolHelper.hpp"
#include "ionclaw/util/Base64.hpp"
#include "ionclaw/util/HttpClient.hpp"
#include "ionclaw/util/MimeType.hpp"
#include "nlohmann/json.hpp"

namespace ionclaw
{
namespace image
{

GeminiImageGenerator::GeminiImageGenerator(std::string providerName)
    : name_(std::move(providerName))
{
}

std::string GeminiImageGenerator::extractModelId(const std::string &model)
{
    auto pos = model.find('/');
    return pos != std::string::npos ? model.substr(pos + 1) : model;
}

std::string GeminiImageGenerator::providerName() const
{
    return name_;
}

std::string GeminiImageGenerator::generate(const std::string &prompt,
                                           const std::string &filename,
                                           const nlohmann::json &params,
                                           const ImageGeneratorContext &context) const
{
    if (!context.config)
    {
        return "Error: configuration not available";
    }

    std::string apiKey = context.config->resolveApiKey(name_);

    if (apiKey.empty())
    {
        return "Error: API key not found for image provider '" + name_ + "'";
    }

    std::string modelId = GeminiImageGenerator::extractModelId(context.model);
    std::string url = "https://generativelanguage.googleapis.com/v1beta/models/" + modelId + ":generateContent";

    // build request parts: reference images BEFORE text prompt
    nlohmann::json parts = nlohmann::json::array();

    if (params.contains("reference_images") && params["reference_images"].is_array())
    {
        for (const auto &refItem : params["reference_images"])
        {
            if (!refItem.is_string())
            {
                continue;
            }

            std::string refPath = refItem.get<std::string>();

            if (refPath.empty())
            {
                continue;
            }

            std::string resolved;

            try
            {
                resolved = ionclaw::tool::builtin::ToolHelper::validateAndResolvePath(
                    context.workspacePath, refPath, context.publicPath);
            }
            catch (...)
            {
                return "Error: reference image path is outside workspace: " + refPath;
            }

            std::string b64 = util::Base64::encodeFromFile(resolved);

            if (b64.empty())
            {
                return "Error: could not read reference image: " + refPath;
            }

            std::string mime = util::MimeType::forPath(resolved);
            parts.push_back({{"inlineData", {{"mimeType", mime}, {"data", b64}}}});
        }
    }

    parts.push_back({{"text", prompt}});

    nlohmann::json contents = nlohmann::json::array();
    contents.push_back({{"parts", parts}});

    // generation config
    nlohmann::json generationConfig = {{"responseModalities", nlohmann::json::array({"TEXT", "IMAGE"})}};

    // image config (aspect ratio and size are pre-validated by GenerateImageTool)
    nlohmann::json imageConfig = nlohmann::json::object();

    if (params.contains("aspect_ratio") && params["aspect_ratio"].is_string())
    {
        auto ar = params["aspect_ratio"].get<std::string>();

        if (!ar.empty())
        {
            imageConfig["aspectRatio"] = ar;
        }
    }

    if (params.contains("size") && params["size"].is_string())
    {
        auto sz = params["size"].get<std::string>();

        if (!sz.empty())
        {
            imageConfig["imageSize"] = sz;
        }
    }

    if (!imageConfig.empty())
    {
        generationConfig["imageConfig"] = imageConfig;
    }

    nlohmann::json requestBody = {{"contents", contents}, {"generationConfig", generationConfig}};

    // google search grounding
    if (params.contains("google_search") && params["google_search"].is_boolean() && params["google_search"].get<bool>())
    {
        requestBody["tools"] = nlohmann::json::array({{{"googleSearch", nlohmann::json::object()}}});
    }

    std::map<std::string, std::string> headers = {
        {"x-goog-api-key", apiKey},
        {"Content-Type", "application/json"},
    };

    // send generation request
    auto response = util::HttpClient::request("POST", url, headers, requestBody.dump(), 120, true);

    if (response.statusCode != 200)
    {
        return "Error: image generation API returned HTTP " + std::to_string(response.statusCode) + ": " +
               response.body.substr(0, 500);
    }

    // parse response and extract base64 image
    auto json = nlohmann::json::parse(response.body);
    auto candidates = json.value("candidates", nlohmann::json::array());

    if (candidates.empty())
    {
        return "Error: no candidates in response";
    }

    auto content = candidates[0].value("content", nlohmann::json::object());
    auto respParts = content.value("parts", nlohmann::json::array());
    std::string b64;

    for (const auto &part : respParts)
    {
        if (part.contains("inlineData"))
        {
            b64 = part["inlineData"].value("data", "");
            break;
        }
    }

    if (b64.empty())
    {
        return "Error: no image data in response (check model supports image generation)";
    }

    // decode and save to public/media
    std::string imageData = util::Base64::decode(b64);
    std::string saved = ImageGeneratorHelper::saveToPublicMedia(
        imageData, context.publicPath, filename, context.config->server.publicUrl);

    if (saved.empty())
    {
        return "Error: cannot write image to public/media";
    }

    return "Image saved: " + saved;
}

} // namespace image
} // namespace ionclaw
