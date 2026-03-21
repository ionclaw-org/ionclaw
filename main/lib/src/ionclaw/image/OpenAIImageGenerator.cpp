#include "ionclaw/image/OpenAIImageGenerator.hpp"

#include "Poco/Net/FilePartSource.h"
#include "Poco/Net/HTMLForm.h"
#include "Poco/Net/HTTPClientSession.h"
#include "Poco/Net/HTTPRequest.h"
#include "Poco/Net/HTTPResponse.h"
#include "Poco/StreamCopier.h"
#include "Poco/URI.h"

#include "ionclaw/config/Config.hpp"
#include "ionclaw/image/ImageGeneratorHelper.hpp"
#include "ionclaw/util/HttpClient.hpp"
#include "ionclaw/util/MimeType.hpp"
#include "ionclaw/util/StringHelper.hpp"
#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace image
{

OpenAIImageGenerator::OpenAIImageGenerator(std::string providerName)
    : name_(std::move(providerName))
{
}

std::string OpenAIImageGenerator::providerName() const
{
    return name_;
}

std::string OpenAIImageGenerator::generate(const std::string &prompt,
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
        baseUrl = "https://api.openai.com";
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

std::string OpenAIImageGenerator::generateImage(const std::string &prompt,
                                                const std::string &filename,
                                                const nlohmann::json &params,
                                                const ImageGeneratorContext &context,
                                                const std::string &apiKey,
                                                const std::string &baseUrl) const
{
    std::string url = baseUrl + "/v1/images/generations";
    std::string modelId = ImageGeneratorHelper::extractModelId(context.model);
    bool isGptImage = modelId.find("gpt-image") != std::string::npos;

    nlohmann::json requestBody = {
        {"model", modelId},
        {"prompt", prompt},
        {"n", 1},
    };

    // gpt-image: output_format selects file format (always returns base64)
    // dall-e: response_format selects delivery encoding
    if (isGptImage)
    {
        requestBody["output_format"] = "png";
    }
    else
    {
        requestBody["response_format"] = "b64_json";
    }

    // resolve size from aspect_ratio
    std::string aspectRatio;

    if (params.contains("aspect_ratio") && params["aspect_ratio"].is_string())
    {
        aspectRatio = params["aspect_ratio"].get<std::string>();
    }

    std::string sizeParam;

    if (params.contains("size") && params["size"].is_string())
    {
        sizeParam = params["size"].get<std::string>();
    }

    if (isGptImage)
    {
        // gpt-image: 1024x1024, 1536x1024, 1024x1536, auto
        if (aspectRatio == "16:9" || aspectRatio == "3:2" || aspectRatio == "4:3")
        {
            requestBody["size"] = "1536x1024";
        }
        else if (aspectRatio == "9:16" || aspectRatio == "2:3" || aspectRatio == "3:4")
        {
            requestBody["size"] = "1024x1536";
        }
        else if (!aspectRatio.empty() && aspectRatio != "1:1")
        {
            requestBody["size"] = "auto";
        }
        else
        {
            requestBody["size"] = "1024x1024";
        }
    }
    else if (modelId.find("dall-e-2") != std::string::npos)
    {
        // dall-e-2: 256x256, 512x512, 1024x1024 only
        requestBody["size"] = "1024x1024";
    }
    else
    {
        // dall-e-3: 1024x1024, 1792x1024, 1024x1792
        if (aspectRatio == "16:9" || aspectRatio == "3:2" || aspectRatio == "21:9")
        {
            requestBody["size"] = "1792x1024";
        }
        else if (aspectRatio == "9:16" || aspectRatio == "2:3")
        {
            requestBody["size"] = "1024x1792";
        }
        else if (sizeParam == "2K" || sizeParam == "4K")
        {
            requestBody["size"] = "1792x1024";
        }
        else
        {
            requestBody["size"] = "1024x1024";
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
        spdlog::error("[OpenAIImageGenerator] API error HTTP {}: {}", response.statusCode, ionclaw::util::StringHelper::utf8SafeTruncate(response.body, 500));
        return "Error: image generation API returned HTTP " + std::to_string(response.statusCode) + ": " +
               ionclaw::util::StringHelper::utf8SafeTruncate(response.body, 500);
    }

    auto saved = ImageGeneratorHelper::decodeAndSave(
        response.body, context.publicPath, filename, context.config->server.publicUrl);

    if (saved.empty())
    {
        return "Error: failed to decode or save generated image";
    }

    return "Image saved: " + saved;
}

std::string OpenAIImageGenerator::editImage(const std::string &prompt,
                                            const std::string &filename,
                                            const nlohmann::json &params,
                                            const ImageGeneratorContext &context,
                                            const std::string &apiKey,
                                            const std::string &baseUrl) const
{
    std::string modelId = ImageGeneratorHelper::extractModelId(context.model);
    bool isGptImage = modelId.find("gpt-image") != std::string::npos;

    // dall-e-3 does not support edits — fall back to generation
    if (!isGptImage && modelId.find("dall-e-3") != std::string::npos)
    {
        spdlog::warn("[OpenAIImageGenerator] {} does not support edits, falling back to generation", modelId);
        return generateImage(prompt, filename, params, context, apiKey, baseUrl);
    }

    bool restrict = !context.config || context.config->tools.restrictToWorkspace;
    auto resolvedRefs = ImageGeneratorHelper::resolveReferencePaths(
        params, context.projectPath, context.workspacePath, context.publicPath, restrict);

    if (resolvedRefs.empty())
    {
        return generateImage(prompt, filename, params, context, apiKey, baseUrl);
    }

    // build multipart form request to /v1/images/edits
    std::string urlStr = baseUrl + "/v1/images/edits";
    Poco::URI uri(urlStr);
    auto host = uri.getHost();
    auto port = uri.getPort();
    auto path = uri.getPathAndQuery();

    if (path.empty())
    {
        path = "/";
    }

    auto session = util::HttpClient::createSession(uri, 120);

    Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_POST, path, Poco::Net::HTTPMessage::HTTP_1_1);
    request.set("Host", host);
    request.set("Authorization", "Bearer " + apiKey);

    Poco::Net::HTMLForm form;
    form.setEncoding(Poco::Net::HTMLForm::ENCODING_MULTIPART);
    form.set("model", modelId);
    form.set("prompt", prompt);
    form.set("n", "1");

    if (isGptImage)
    {
        form.set("output_format", "png");
    }
    else
    {
        form.set("response_format", "b64_json");
    }

    // resolve size from aspect_ratio
    std::string aspectRatio;

    if (params.contains("aspect_ratio") && params["aspect_ratio"].is_string())
    {
        aspectRatio = params["aspect_ratio"].get<std::string>();
    }

    if (isGptImage)
    {
        // gpt-image edit: 1024x1024, 1536x1024, 1024x1536, auto
        if (aspectRatio == "16:9" || aspectRatio == "3:2" || aspectRatio == "4:3")
        {
            form.set("size", "1536x1024");
        }
        else if (aspectRatio == "9:16" || aspectRatio == "2:3" || aspectRatio == "3:4")
        {
            form.set("size", "1024x1536");
        }
        else
        {
            form.set("size", "1024x1024");
        }
    }
    else
    {
        // dall-e-2 edit: only 256x256, 512x512, 1024x1024
        form.set("size", "1024x1024");
    }

    // add reference images
    for (const auto &ref : resolvedRefs)
    {
        auto mimeType = util::MimeType::forPath(ref);

        if (isGptImage)
        {
            // gpt-image supports multiple images via "image[]"
            form.addPart("image[]", new Poco::Net::FilePartSource(ref, mimeType));
        }
        else
        {
            // dall-e-2 accepts only one image
            form.addPart("image", new Poco::Net::FilePartSource(ref, mimeType));
            break;
        }
    }

    form.prepareSubmit(request);
    form.write(session->sendRequest(request));

    Poco::Net::HTTPResponse httpResp;
    auto &rs = session->receiveResponse(httpResp);
    std::string respBody;
    Poco::StreamCopier::copyToString(rs, respBody);

    auto status = static_cast<int>(httpResp.getStatus());

    if (status != 200)
    {
        spdlog::error("[OpenAIImageGenerator] Edit API error HTTP {}: {}", status, ionclaw::util::StringHelper::utf8SafeTruncate(respBody, 500));
        return "Error: image edit API returned HTTP " + std::to_string(status) + ": " +
               ionclaw::util::StringHelper::utf8SafeTruncate(respBody, 500);
    }

    auto saved = ImageGeneratorHelper::decodeAndSave(
        respBody, context.publicPath, filename, context.config->server.publicUrl);

    if (saved.empty())
    {
        return "Error: failed to decode or save edited image";
    }

    return "Image saved: " + saved;
}

} // namespace image
} // namespace ionclaw
