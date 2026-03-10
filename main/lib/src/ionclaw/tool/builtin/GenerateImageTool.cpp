#include "ionclaw/tool/builtin/GenerateImageTool.hpp"

#include "ionclaw/bus/EventDispatcher.hpp"
#include "ionclaw/config/Config.hpp"
#include "ionclaw/image/ImageGeneratorRegistry.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

// valid aspect ratio values
const std::set<std::string> GenerateImageTool::VALID_ASPECT_RATIOS = {
    "1:1",
    "1:4",
    "1:8",
    "2:3",
    "3:2",
    "3:4",
    "4:1",
    "4:3",
    "4:5",
    "5:4",
    "8:1",
    "9:16",
    "16:9",
    "21:9",
};

// valid resolution sizes
const std::set<std::string> GenerateImageTool::VALID_SIZES = {"1K", "2K", "4K"};

// execute image generation
ToolResult GenerateImageTool::execute(const nlohmann::json &params, const ToolContext &context)
{
    std::string prompt = params.at("prompt").get<std::string>();
    std::string filename = params.at("filename").get<std::string>();

    if (!context.config)
    {
        return "Error: configuration not available";
    }

    // validate aspect ratio
    std::string aspectRatio;

    if (params.contains("aspect_ratio") && params["aspect_ratio"].is_string())
    {
        aspectRatio = params["aspect_ratio"].get<std::string>();
    }
    else
    {
        aspectRatio = context.config->image.aspectRatio;
    }

    if (!aspectRatio.empty() && VALID_ASPECT_RATIOS.find(aspectRatio) == VALID_ASPECT_RATIOS.end())
    {
        return "Error: invalid aspect_ratio '" + aspectRatio + "'. Valid: 1:1, 1:4, 1:8, 2:3, 3:2, 3:4, 4:1, 4:3, 4:5, 5:4, 8:1, 9:16, 16:9, 21:9";
    }

    // validate size
    std::string size;

    if (params.contains("size") && params["size"].is_string())
    {
        size = params["size"].get<std::string>();
    }
    else
    {
        size = context.config->image.size.empty() ? "1K" : context.config->image.size;
    }

    if (!size.empty() && VALID_SIZES.find(size) == VALID_SIZES.end())
    {
        return "Error: invalid size '" + size + "'. Valid: 1K, 2K, 4K";
    }

    // build final prompt with style and negative hints
    std::string finalPrompt = prompt;

    if (params.contains("style") && params["style"].is_string())
    {
        auto style = params["style"].get<std::string>();

        if (!style.empty())
        {
            finalPrompt = "Style: " + style + ". " + finalPrompt;
        }
    }

    if (params.contains("negative_prompt") && params["negative_prompt"].is_string())
    {
        auto neg = params["negative_prompt"].get<std::string>();

        if (!neg.empty())
        {
            finalPrompt = finalPrompt + ". Avoid: " + neg;
        }
    }

    // helper to broadcast a warning to the chat UI
    auto broadcastWarning = [&context](const std::string &text)
    {
        if (context.dispatcher)
        {
            context.dispatcher->broadcast("chat:warning", {
                                                              {"content", text},
                                                              {"chat_id", context.sessionKey},
                                                              {"agent_name", context.agentName},
                                                          });
        }
    };

    // resolve image model
    std::string model = context.config->image.model;

    if (model.empty())
    {
        auto msg = "No image generation model configured. Configure it in Settings (image.model).";
        broadcastWarning(msg);
        return "Error: " + std::string(msg);
    }

    // resolve image generator provider
    auto providerConfig = context.config->resolveProvider(model);
    image::ImageGenerator *generator = image::ImageGeneratorRegistry::instance().get(providerConfig.name);

    if (!generator)
    {
        auto msg = "No image generator available for provider '" + providerConfig.name +
                   "'. Supported: gemini, google, openai, grok.";
        broadcastWarning(msg);
        return "Error: " + msg;
    }

    // resolve api key
    std::string apiKey = context.config->resolveApiKey(providerConfig.name);

    if (apiKey.empty())
    {
        auto msg = "API key not found for image provider '" + providerConfig.name +
                   "'. Check credentials." + providerConfig.credential + ".key in Settings.";
        broadcastWarning(msg);
        return "Error: " + msg;
    }

    // build validated params to pass to generator
    nlohmann::json validatedParams = params;
    validatedParams["aspect_ratio"] = aspectRatio;
    validatedParams["size"] = size;

    // build generator context
    image::ImageGeneratorContext imgContext;
    imgContext.workspacePath = context.workspacePath;
    imgContext.publicPath = context.publicPath;
    imgContext.model = model;
    imgContext.providerName = providerConfig.name;
    imgContext.config = context.config;

    try
    {
        return generator->generate(finalPrompt, filename, validatedParams, imgContext);
    }
    catch (const std::exception &e)
    {
        return "Error: image generation failed: " + std::string(e.what());
    }
}

// schema definition
ToolSchema GenerateImageTool::schema() const
{
    return {
        "generate_image",
        "Generate an image from a text prompt and save it to the public media directory. "
        "Supports aspect ratio, resolution, reference images, style hints, and Google Search "
        "grounding for real-time data. Reference images are used as visual context — place logos, "
        "style references, or content to incorporate.",
        {{"type", "object"},
         {"properties",
          {{"prompt", {{"type", "string"}, {"description", "Detailed description of the image to generate. Include style, subject, setting, action, and composition."}}},
           {"filename", {{"type", "string"}, {"description", "Output filename (e.g. poster.png, banner.jpg)"}}},
           {"aspect_ratio", {{"type", "string"}, {"description", "Image aspect ratio. Options: 1:1, 16:9, 9:16, 4:3, 3:4, 3:2, 2:3, 4:5, 5:4, 21:9. Use 1:1 for social posts, 16:9 for banners, 9:16 for stories."}}},
           {"size", {{"type", "string"}, {"enum", nlohmann::json::array({"1K", "2K", "4K"})}, {"description", "Image resolution. Options: 1K (default), 2K, 4K. Use 2K for social media, 4K for print."}}},
           {"reference_images", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Paths to reference images (use the exact path from [media: path (type)] annotations in the conversation). These are sent as visual context BEFORE the prompt. For logos, describe in the prompt how to use them."}}},
           {"style", {{"type", "string"}, {"description", "Style hint (e.g. photorealistic, illustration, watercolor, minimal, cinematic, digital art, vector, pixel art)"}}},
           {"negative_prompt", {{"type", "string"}, {"description", "What to avoid in the image (e.g. blurry, text, watermark)"}}},
           {"google_search", {{"type", "boolean"}, {"description", "Enable Google Search grounding for real-time data (stock prices, weather, current events, charts)"}}}}},
         {"required", nlohmann::json::array({"prompt", "filename"})}}};
}

} // namespace builtin
} // namespace tool
} // namespace ionclaw
