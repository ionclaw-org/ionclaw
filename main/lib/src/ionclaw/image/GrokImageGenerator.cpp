#include "ionclaw/image/GrokImageGenerator.hpp"

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

GrokImageGenerator::GrokImageGenerator(std::string providerName)
    : name_(std::move(providerName))
{
}

std::string GrokImageGenerator::providerName() const
{
    return name_;
}

std::string GrokImageGenerator::generate(const std::string &prompt,
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

    std::string baseUrl = context.config->resolveBaseUrl(context.providerName);

    if (baseUrl.empty())
    {
        baseUrl = "https://api.x.ai";
    }

    if (baseUrl.back() == '/')
    {
        baseUrl.pop_back();
    }

    bool hasRefs = params.contains("reference_images") && params["reference_images"].is_array() &&
                   !params["reference_images"].empty();

    if (hasRefs)
    {
        return editImage(prompt, filename, params, context, apiKey, baseUrl);
    }

    return generateImage(prompt, filename, params, context, apiKey, baseUrl);
}

std::string GrokImageGenerator::generateImage(const std::string &prompt,
                                              const std::string &filename,
                                              const nlohmann::json &params,
                                              const ImageGeneratorContext &context,
                                              const std::string &apiKey,
                                              const std::string &baseUrl) const
{
    std::string url = baseUrl + "/v1/images/generations";
    std::string modelId = ImageGeneratorHelper::extractModelId(context.model);

    nlohmann::json requestBody = {
        {"model", modelId},
        {"prompt", prompt},
        {"n", 1},
        {"response_format", "b64_json"},
    };

    // grok accepts aspect_ratio directly
    if (params.contains("aspect_ratio") && params["aspect_ratio"].is_string())
    {
        auto ar = params["aspect_ratio"].get<std::string>();

        if (!ar.empty())
        {
            requestBody["aspect_ratio"] = ar;
        }
    }

    // grok uses "resolution" (1k, 2k) instead of "size"
    if (params.contains("size") && params["size"].is_string())
    {
        auto sz = params["size"].get<std::string>();

        if (sz == "2K" || sz == "4K")
        {
            requestBody["resolution"] = "2k";
        }
        else
        {
            requestBody["resolution"] = "1k";
        }
    }

    std::map<std::string, std::string> headers = {
        {"Authorization", "Bearer " + apiKey},
        {"Content-Type", "application/json"},
    };

    auto requestStr = requestBody.dump();
    auto response = util::HttpClient::request("POST", url, headers, requestStr, 120, true);

    if (response.statusCode != 200)
    {
        spdlog::error("[GrokImageGenerator] API error HTTP {}: {}", response.statusCode, response.body.substr(0, 500));
        return "Error: image generation API returned HTTP " + std::to_string(response.statusCode) + ": " +
               response.body.substr(0, 500);
    }

    auto saved = ImageGeneratorHelper::decodeAndSave(
        response.body, context.publicPath, filename, context.config->server.publicUrl);

    if (saved.empty())
    {
        return "Error: failed to decode or save generated image";
    }

    return "Image saved: " + saved;
}

std::string GrokImageGenerator::editImage(const std::string &prompt,
                                          const std::string &filename,
                                          const nlohmann::json &params,
                                          const ImageGeneratorContext &context,
                                          const std::string &apiKey,
                                          const std::string &baseUrl) const
{
    auto resolvedRefs = ImageGeneratorHelper::resolveReferencePaths(
        params, context.workspacePath, context.publicPath);

    if (resolvedRefs.empty())
    {
        return generateImage(prompt, filename, params, context, apiKey, baseUrl);
    }

    std::string url = baseUrl + "/v1/images/edits";
    std::string modelId = ImageGeneratorHelper::extractModelId(context.model);

    // grok edit uses JSON with base64 data URIs (not multipart)
    nlohmann::json requestBody = {
        {"model", modelId},
        {"prompt", prompt},
        {"n", 1},
    };

    if (params.contains("aspect_ratio") && params["aspect_ratio"].is_string())
    {
        auto ar = params["aspect_ratio"].get<std::string>();

        if (!ar.empty())
        {
            requestBody["aspect_ratio"] = ar;
        }
    }

    // encode reference images as base64 data URIs
    // single: "image" object; multiple: "images" array (up to 3)
    nlohmann::json imageEntries = nlohmann::json::array();

    for (size_t i = 0; i < resolvedRefs.size() && i < 3; ++i)
    {
        std::string b64 = util::Base64::encodeFromFile(resolvedRefs[i]);

        if (b64.empty())
        {
            spdlog::error("[GrokImageGenerator] Could not read reference image: {}", resolvedRefs[i]);
            continue;
        }

        std::string mime = util::MimeType::forPath(resolvedRefs[i]);
        std::string dataUri = "data:" + mime + ";base64," + b64;
        imageEntries.push_back({{"url", dataUri}, {"type", "image_url"}});
    }

    if (imageEntries.empty())
    {
        return generateImage(prompt, filename, params, context, apiKey, baseUrl);
    }

    if (imageEntries.size() == 1)
    {
        requestBody["image"] = imageEntries[0];
    }
    else
    {
        requestBody["images"] = imageEntries;
    }

    std::map<std::string, std::string> headers = {
        {"Authorization", "Bearer " + apiKey},
        {"Content-Type", "application/json"},
    };

    auto requestStr = requestBody.dump();
    auto response = util::HttpClient::request("POST", url, headers, requestStr, 120, true);

    if (response.statusCode != 200)
    {
        spdlog::error("[GrokImageGenerator] Edit API error HTTP {}: {}", response.statusCode, response.body.substr(0, 500));
        return "Error: image edit API returned HTTP " + std::to_string(response.statusCode) + ": " +
               response.body.substr(0, 500);
    }

    auto saved = ImageGeneratorHelper::decodeAndSave(
        response.body, context.publicPath, filename, context.config->server.publicUrl);

    if (saved.empty())
    {
        return "Error: failed to decode or save edited image";
    }

    return "Image saved: " + saved;
}

} // namespace image
} // namespace ionclaw
