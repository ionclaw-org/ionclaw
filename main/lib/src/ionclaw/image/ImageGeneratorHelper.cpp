#include "ionclaw/image/ImageGeneratorHelper.hpp"

#include <filesystem>
#include <fstream>

#include "ionclaw/tool/builtin/ToolHelper.hpp"
#include "ionclaw/util/Base64.hpp"
#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace image
{

std::string ImageGeneratorHelper::saveToPublicMedia(const std::string &imageData,
                                                    const std::string &publicPath,
                                                    const std::string &filename,
                                                    const std::string &publicUrl)
{
    namespace fs = std::filesystem;

    std::string mediaDir = publicPath + "/media";
    std::error_code ec;
    fs::create_directories(mediaDir, ec);

    // sanitize filename to prevent path traversal
    auto safeName = fs::path(filename).filename().string();

    if (safeName.empty() || safeName == "." || safeName == "..")
    {
        spdlog::error("[ImageGenerator] Invalid filename: {}", filename);
        return "";
    }

    std::string outputPath = mediaDir + "/" + safeName;
    std::ofstream outFile(outputPath, std::ios::binary);

    if (!outFile.is_open())
    {
        return "";
    }

    outFile.write(imageData.data(), static_cast<std::streamsize>(imageData.size()));
    outFile.flush();

    if (!outFile.good())
    {
        spdlog::error("[ImageGenerator] Failed to write image to: {}", outputPath);
        outFile.close();
        return "";
    }

    outFile.close();

    std::string relativePath = "public/media/" + safeName;

    if (!publicUrl.empty())
    {
        std::string base = publicUrl;

        if (!base.empty() && base.back() == '/')
        {
            base.pop_back();
        }

        return base + "/" + relativePath;
    }

    return relativePath;
}

std::string ImageGeneratorHelper::cleanReferencePath(const std::string &raw)
{
    std::string path = raw;

    // strip [media: prefix
    if (path.size() > 8 && path.substr(0, 8) == "[media: ")
    {
        path = path.substr(8);

        // strip trailing " (type)]" — find last " ("
        auto paren = path.rfind(" (");

        if (paren != std::string::npos)
        {
            path = path.substr(0, paren);
        }
        else if (!path.empty() && path.back() == ']')
        {
            path.pop_back();
        }
    }

    // trim whitespace
    while (!path.empty() && (path.front() == ' ' || path.front() == '\t'))
    {
        path.erase(path.begin());
    }

    while (!path.empty() && (path.back() == ' ' || path.back() == '\t'))
    {
        path.pop_back();
    }

    return path;
}

std::string ImageGeneratorHelper::extractModelId(const std::string &model)
{
    auto pos = model.find('/');
    return pos != std::string::npos ? model.substr(pos + 1) : model;
}

std::vector<std::string> ImageGeneratorHelper::resolveReferencePaths(const nlohmann::json &params,
                                                                     const std::string &workspacePath,
                                                                     const std::string &publicPath,
                                                                     bool restrictToWorkspace,
                                                                     const std::string &projectPath)
{
    std::vector<std::string> resolved;

    if (!params.contains("reference_images") || !params["reference_images"].is_array())
    {
        return resolved;
    }

    for (const auto &item : params["reference_images"])
    {
        if (!item.is_string() || item.get<std::string>().empty())
        {
            continue;
        }

        std::string refPath = cleanReferencePath(item.get<std::string>());

        if (refPath.empty())
        {
            continue;
        }

        try
        {
            auto full = tool::builtin::ToolHelper::validateAndResolvePath(workspacePath, refPath, publicPath, restrictToWorkspace, projectPath);
            resolved.push_back(full);
        }
        catch (const std::exception &e)
        {
            spdlog::error("[ImageGenerator] Path resolution failed for '{}': {}", refPath, e.what());
        }
    }

    return resolved;
}

std::string ImageGeneratorHelper::decodeAndSave(const std::string &responseBody,
                                                const std::string &publicPath,
                                                const std::string &filename,
                                                const std::string &publicUrl)
{
    auto json = nlohmann::json::parse(responseBody, nullptr, false);

    if (json.is_discarded())
    {
        spdlog::error("[ImageGenerator] Failed to parse response JSON");
        return "";
    }

    auto data = json.value("data", nlohmann::json::array());

    if (data.empty())
    {
        spdlog::error("[ImageGenerator] No image data in response");
        return "";
    }

    std::string b64 = data[0].value("b64_json", "");

    if (b64.empty())
    {
        spdlog::error("[ImageGenerator] No base64 image data in response");
        return "";
    }

    std::string imageData = util::Base64::decode(b64);
    return saveToPublicMedia(imageData, publicPath, filename, publicUrl);
}

} // namespace image
} // namespace ionclaw
