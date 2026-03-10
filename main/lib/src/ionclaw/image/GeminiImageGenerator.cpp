#include "ionclaw/image/GeminiImageGenerator.hpp"

#include "Poco/URI.h"

#include "ionclaw/config/Config.hpp"
#include "ionclaw/image/ImageGeneratorHelper.hpp"
#include "ionclaw/util/Base64.hpp"
#include "ionclaw/util/HttpClient.hpp"
#include "ionclaw/util/MimeType.hpp"
#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace image
{

GeminiImageGenerator::GeminiImageGenerator(std::string providerName)
    : name_(std::move(providerName))
{
}

std::string GeminiImageGenerator::providerName() const
{
    return name_;
}

bool GeminiImageGenerator::isImagenModel(const std::string &modelId)
{
    return modelId.find("imagen") != std::string::npos;
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

    std::string apiKey = context.config->resolveApiKey(context.providerName);

    if (apiKey.empty())
    {
        return "Error: API key not found for image provider '" + context.providerName + "'";
    }

    std::string modelId = ImageGeneratorHelper::extractModelId(context.model);

    // resolve base URL, stripping any OpenAI-compat path suffix (e.g. /v1beta/openai)
    std::string baseUrl = context.config->resolveBaseUrl(context.providerName);

    if (baseUrl.empty())
    {
        baseUrl = "https://generativelanguage.googleapis.com";
    }

    // the default resolveBaseUrl returns the OpenAI-compat endpoint
    // (e.g. .../v1beta/openai); strip path to get the native API host
    Poco::URI baseUri(baseUrl);
    baseUrl = baseUri.getScheme() + "://" + baseUri.getAuthority();

    // imagen models use :predict endpoint; gemini models use :generateContent
    if (isImagenModel(modelId))
    {
        return predict(prompt, filename, params, context, apiKey, baseUrl, modelId);
    }

    return generateContent(prompt, filename, params, context, apiKey, baseUrl, modelId);
}

std::string GeminiImageGenerator::generateContent(const std::string &prompt,
                                                  const std::string &filename,
                                                  const nlohmann::json &params,
                                                  const ImageGeneratorContext &context,
                                                  const std::string &apiKey,
                                                  const std::string &baseUrl,
                                                  const std::string &modelId) const
{
    std::string url = baseUrl + "/v1beta/models/" + modelId + ":generateContent";

    // build request parts: reference images BEFORE text prompt
    nlohmann::json parts = nlohmann::json::array();

    auto resolvedRefs = ImageGeneratorHelper::resolveReferencePaths(
        params, context.workspacePath, context.publicPath);

    for (const auto &resolved : resolvedRefs)
    {
        std::string b64 = util::Base64::encodeFromFile(resolved);

        if (b64.empty())
        {
            spdlog::error("[GeminiImageGenerator] Could not read reference image: {}", resolved);
            return "Error: could not read reference image: " + resolved;
        }

        std::string mime = util::MimeType::forPath(resolved);
        parts.push_back({{"inlineData", {{"mimeType", mime}, {"data", b64}}}});
    }

    parts.push_back({{"text", prompt}});

    nlohmann::json contents = nlohmann::json::array();
    contents.push_back({{"parts", parts}});

    // generation config
    nlohmann::json generationConfig = {{"responseModalities", nlohmann::json::array({"TEXT", "IMAGE"})}};

    // image config
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

    auto requestStr = requestBody.dump();
    auto response = util::HttpClient::request("POST", url, headers, requestStr, 120, true);

    if (response.statusCode != 200)
    {
        spdlog::error("[GeminiImageGenerator] API error HTTP {}: {}", response.statusCode, response.body.substr(0, 500));
        return "Error: image generation API returned HTTP " + std::to_string(response.statusCode) + ": " +
               response.body.substr(0, 500);
    }

    // parse response and extract base64 image
    auto json = nlohmann::json::parse(response.body, nullptr, false);

    if (json.is_discarded())
    {
        spdlog::error("[GeminiImageGenerator] Failed to parse response JSON");
        return "Error: failed to parse image generation response";
    }

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

    std::string imageData = util::Base64::decode(b64);
    std::string saved = ImageGeneratorHelper::saveToPublicMedia(
        imageData, context.publicPath, filename, context.config->server.publicUrl);

    if (saved.empty())
    {
        return "Error: cannot write image to public/media";
    }

    return "Image saved: " + saved;
}

std::string GeminiImageGenerator::predict(const std::string &prompt,
                                          const std::string &filename,
                                          const nlohmann::json &params,
                                          const ImageGeneratorContext &context,
                                          const std::string &apiKey,
                                          const std::string &baseUrl,
                                          const std::string &modelId) const
{
    // imagen models use the :predict endpoint (text-to-image only, no reference images)
    std::string url = baseUrl + "/v1beta/models/" + modelId + ":predict";

    nlohmann::json instance = {{"prompt", prompt}};
    nlohmann::json parameters = {{"numberOfImages", 1}};

    if (params.contains("aspect_ratio") && params["aspect_ratio"].is_string())
    {
        auto ar = params["aspect_ratio"].get<std::string>();

        if (!ar.empty())
        {
            parameters["aspectRatio"] = ar;
        }
    }

    if (params.contains("size") && params["size"].is_string())
    {
        auto sz = params["size"].get<std::string>();

        if (!sz.empty())
        {
            parameters["imageSize"] = sz;
        }
    }

    nlohmann::json requestBody = {
        {"instances", nlohmann::json::array({instance})},
        {"parameters", parameters},
    };

    std::map<std::string, std::string> headers = {
        {"x-goog-api-key", apiKey},
        {"Content-Type", "application/json"},
    };

    auto requestStr = requestBody.dump();
    auto response = util::HttpClient::request("POST", url, headers, requestStr, 120, true);

    if (response.statusCode != 200)
    {
        spdlog::error("[GeminiImageGenerator] Imagen API error HTTP {}: {}", response.statusCode, response.body.substr(0, 500));
        return "Error: image generation API returned HTTP " + std::to_string(response.statusCode) + ": " +
               response.body.substr(0, 500);
    }

    auto json = nlohmann::json::parse(response.body, nullptr, false);

    if (json.is_discarded())
    {
        spdlog::error("[GeminiImageGenerator] Failed to parse Imagen response JSON");
        return "Error: failed to parse image generation response";
    }

    // imagen response: predictions[0].bytesBase64Encoded
    auto predictions = json.value("predictions", nlohmann::json::array());

    if (predictions.empty())
    {
        return "Error: no predictions in response";
    }

    std::string b64 = predictions[0].value("bytesBase64Encoded", "");

    if (b64.empty())
    {
        return "Error: no image data in Imagen response";
    }

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
