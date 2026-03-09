#include "ionclaw/image/OpenAIStyleImageGenerator.hpp"

#include "Poco/Net/FilePartSource.h"
#include "Poco/Net/HTMLForm.h"
#include "Poco/Net/HTTPClientSession.h"
#include "Poco/Net/HTTPRequest.h"
#include "Poco/Net/HTTPResponse.h"
#include "Poco/StreamCopier.h"
#include "Poco/URI.h"

#ifdef IONCLAW_HAS_SSL
#include "Poco/Net/Context.h"
#include "Poco/Net/HTTPSClientSession.h"
#endif

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

OpenAIStyleImageGenerator::OpenAIStyleImageGenerator(std::string providerName)
    : name_(std::move(providerName))
{
}

std::string OpenAIStyleImageGenerator::providerName() const
{
    return name_;
}

std::string OpenAIStyleImageGenerator::generate(const std::string &prompt,
                                                const std::string &filename,
                                                const nlohmann::json &params,
                                                const ImageGeneratorContext &context) const
{
    if (!context.config)
    {
        return "Error: configuration not available";
    }

    // resolve api key and base url
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

    // route to edit endpoint if reference images are provided
    bool hasRefs = params.contains("reference_images") && params["reference_images"].is_array() &&
                   !params["reference_images"].empty();

    if (hasRefs)
    {
        return editImage(prompt, filename, params, context, apiKey, baseUrl);
    }

    return generateImage(prompt, filename, params, context, apiKey, baseUrl);
}

std::string OpenAIStyleImageGenerator::generateImage(const std::string &prompt,
                                                     const std::string &filename,
                                                     const nlohmann::json &params,
                                                     const ImageGeneratorContext &context,
                                                     const std::string &apiKey,
                                                     const std::string &baseUrl) const
{
    std::string url = baseUrl + "/v1/images/generations";

    // build request body
    nlohmann::json requestBody = {
        {"model", context.model},
        {"prompt", prompt},
        {"n", 1},
        {"response_format", "b64_json"},
    };

    // map size param to pixel dimensions
    if (params.contains("size") && params["size"].is_string())
    {
        std::string s = params["size"].get<std::string>();

        if (s == "1K")
        {
            requestBody["size"] = "1024x1024";
        }
        else if (s == "2K")
        {
            requestBody["size"] = "2048x2048";
        }
        else if (s == "4K")
        {
            requestBody["size"] = "4096x4096";
        }
    }

    if (params.contains("aspect_ratio") && params["aspect_ratio"].is_string())
    {
        auto ar = params["aspect_ratio"].get<std::string>();

        if (!ar.empty())
        {
            requestBody["aspect_ratio"] = ar;
        }
    }

    std::map<std::string, std::string> headers = {
        {"Authorization", "Bearer " + apiKey},
        {"Content-Type", "application/json"},
    };

    auto response = util::HttpClient::request("POST", url, headers, requestBody.dump(), 120, true);

    if (response.statusCode != 200)
    {
        return "Error: image generation API returned HTTP " + std::to_string(response.statusCode) + ": " +
               response.body.substr(0, 500);
    }

    // parse response
    auto json = nlohmann::json::parse(response.body);
    auto data = json.value("data", nlohmann::json::array());

    if (data.empty())
    {
        return "Error: no image data in response";
    }

    std::string b64 = data[0].value("b64_json", "");

    if (b64.empty())
    {
        return "Error: no base64 image data in response";
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

std::string OpenAIStyleImageGenerator::editImage(const std::string &prompt,
                                                 const std::string &filename,
                                                 const nlohmann::json &params,
                                                 const ImageGeneratorContext &context,
                                                 const std::string &apiKey,
                                                 const std::string &baseUrl) const
{
    // resolve the first reference image (OpenAI edit endpoint accepts one image)
    std::string refPath;

    for (const auto &item : params["reference_images"])
    {
        if (item.is_string() && !item.get<std::string>().empty())
        {
            refPath = item.get<std::string>();
            break;
        }
    }

    if (refPath.empty())
    {
        return generateImage(prompt, filename, params, context, apiKey, baseUrl);
    }

    std::string resolvedRef;

    try
    {
        resolvedRef = tool::builtin::ToolHelper::validateAndResolvePath(
            context.workspacePath, refPath, context.publicPath);
    }
    catch (...)
    {
        return "Error: reference image path is outside workspace: " + refPath;
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

    std::unique_ptr<Poco::Net::HTTPClientSession> session;

#ifdef IONCLAW_HAS_SSL
    if (uri.getScheme() == "https")
    {
#ifdef _WIN32
        auto ctx = new Poco::Net::Context(
            Poco::Net::Context::CLIENT_USE, "");
#else
        auto ctx = new Poco::Net::Context(
            Poco::Net::Context::CLIENT_USE, "", "", "",
            Poco::Net::Context::VERIFY_NONE, 9, true,
            "ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH");
#endif
        session = std::make_unique<Poco::Net::HTTPSClientSession>(host, port, ctx);
    }
    else
#endif
    {
        session = std::make_unique<Poco::Net::HTTPClientSession>(host, port);
    }

    session->setTimeout(Poco::Timespan(120, 0));

    Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_POST, path, Poco::Net::HTTPMessage::HTTP_1_1);
    request.set("Host", host);
    request.set("Authorization", "Bearer " + apiKey);

    // build multipart form
    auto mimeType = util::MimeType::forPath(resolvedRef);

    Poco::Net::HTMLForm form;
    form.setEncoding(Poco::Net::HTMLForm::ENCODING_MULTIPART);
    form.set("model", context.model);
    form.set("prompt", prompt);
    form.set("response_format", "b64_json");
    form.set("n", "1");
    form.addPart("image", new Poco::Net::FilePartSource(resolvedRef, mimeType));
    form.prepareSubmit(request);
    form.write(session->sendRequest(request));

    Poco::Net::HTTPResponse httpResp;
    auto &rs = session->receiveResponse(httpResp);
    std::string respBody;
    Poco::StreamCopier::copyToString(rs, respBody);

    auto status = static_cast<int>(httpResp.getStatus());

    if (status != 200)
    {
        return "Error: image edit API returned HTTP " + std::to_string(status) + ": " +
               respBody.substr(0, 500);
    }

    // parse response
    auto json = nlohmann::json::parse(respBody);
    auto data = json.value("data", nlohmann::json::array());

    if (data.empty())
    {
        return "Error: no image data in edit response";
    }

    std::string b64 = data[0].value("b64_json", "");

    if (b64.empty())
    {
        return "Error: no base64 image data in edit response";
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
