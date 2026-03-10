#include "ionclaw/tool/builtin/ImageOpsTool.hpp"

#include <cmath>
#include <filesystem>
#include <sstream>
#include <vector>

#include "ionclaw/tool/builtin/ToolHelper.hpp"

#ifdef IONCLAW_HAS_STB_IMAGE_WRITE
#include "stb_image.h"
#include "stb_image_resize2.h"
#include "stb_image_write.h"
#endif

namespace ionclaw
{
namespace tool
{
namespace builtin
{

#ifdef IONCLAW_HAS_STB_IMAGE_WRITE
bool ImageOpsTool::parseHexColor(const std::string &hex, unsigned char &r, unsigned char &g, unsigned char &b)
{
    if (hex.size() != 6)
    {
        return false;
    }

    // convert a single hex character to its integer value
    auto digit = [](char c) -> int
    {
        if (c >= '0' && c <= '9')
        {
            return c - '0';
        }

        if (c >= 'a' && c <= 'f')
        {
            return c - 'a' + 10;
        }

        if (c >= 'A' && c <= 'F')
        {
            return c - 'A' + 10;
        }

        return -1;
    };

    // parse each channel pair
    int r0 = digit(hex[0]), r1 = digit(hex[1]);
    int g0 = digit(hex[2]), g1 = digit(hex[3]);
    int b0 = digit(hex[4]), b1 = digit(hex[5]);

    if (r0 < 0 || r1 < 0 || g0 < 0 || g1 < 0 || b0 < 0 || b1 < 0)
    {
        return false;
    }

    r = static_cast<unsigned char>(r0 * 16 + r1);
    g = static_cast<unsigned char>(g0 * 16 + g1);
    b = static_cast<unsigned char>(b0 * 16 + b1);
    return true;
}
#endif

ToolResult ImageOpsTool::execute(const nlohmann::json &params, const ToolContext &context)
{
#ifndef IONCLAW_HAS_STB_IMAGE_WRITE
    (void)params;
    (void)context;
    return "Error: image_ops requires stb_image_write (not available in this build).";
#else
    // validate operation parameter
    std::string op = params.value("operation", "");

    if (op.empty())
    {
        return "Error: missing 'operation'. Use create, resize, draw_rect, or overlay.";
    }

    // path resolver helper
    auto resolve = [&](const std::string &path) -> std::string
    {
        try
        {
            return ToolHelper::validateAndResolvePath(context.workspacePath, path, context.publicPath);
        }
        catch (...)
        {
            return "";
        }
    };

    // create operation
    if (op == "create")
    {
        int width = params.value("width", 512);
        int height = params.value("height", 512);
        std::string outputPath = params.value("output_path", "");
        std::string background = params.value("background", "gradient");
        std::string colorHex = params.value("color", "ffffff");

        if (outputPath.empty())
        {
            return "Error: create requires output_path.";
        }

        std::string resolvedOut = resolve(outputPath);

        if (resolvedOut.empty())
        {
            return "Error: output_path is outside workspace or public: " + outputPath;
        }

        if (width < 1 || height < 1 || width > 8192 || height > 8192)
        {
            return "Error: width and height must be between 1 and 8192.";
        }

        // fill pixels based on background mode
        std::vector<unsigned char> pixels(static_cast<size_t>(width * height * 3));

        if (background == "solid")
        {
            unsigned char r = 255, g = 255, b = 255;

            if (!parseHexColor(colorHex, r, g, b))
            {
                return "Error: color must be hex RRGGBB (e.g. ff0000 for red).";
            }

            for (size_t i = 0; i < pixels.size(); i += 3)
            {
                pixels[i + 0] = r;
                pixels[i + 1] = g;
                pixels[i + 2] = b;
            }
        }
        else
        {
            // gradient fill
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    size_t i = static_cast<size_t>((y * width + x) * 3);
                    pixels[i + 0] = static_cast<unsigned char>((x * 255) / (width > 1 ? width : 1));
                    pixels[i + 1] = static_cast<unsigned char>((y * 255) / (height > 1 ? height : 1));
                    pixels[i + 2] = 128;
                }
            }
        }

        // ensure parent directory exists
        std::filesystem::path p(resolvedOut);

        if (!p.parent_path().empty() && !std::filesystem::exists(p.parent_path()))
        {
            std::error_code ec;
            std::filesystem::create_directories(p.parent_path(), ec);
        }

        // write output image
        if (stbi_write_png(resolvedOut.c_str(), width, height, 3, pixels.data(), 0) == 0)
        {
            return "Error: failed to write image to " + outputPath;
        }

        return "Image created: " + outputPath;
    }

    // resize operation
    if (op == "resize")
    {
        std::string inputPath = params.value("input_path", "");
        std::string outputPath = params.value("output_path", "");
        int outW = params.value("width", 0);
        int outH = params.value("height", 0);

        if (inputPath.empty() || outputPath.empty() || outW < 1 || outH < 1)
        {
            return "Error: resize requires input_path, output_path, width, height.";
        }

        std::string inResolved = resolve(inputPath);
        std::string outResolved = resolve(outputPath);

        if (inResolved.empty())
        {
            return "Error: input_path is outside workspace or public: " + inputPath;
        }

        if (outResolved.empty())
        {
            return "Error: output_path is outside workspace or public: " + outputPath;
        }

        // load source image
        int w = 0, h = 0, ch = 0;
        unsigned char *img = stbi_load(inResolved.c_str(), &w, &h, &ch, 3);

        if (!img)
        {
            return "Error: could not load image: " + inputPath;
        }

        // perform resize
        std::vector<unsigned char> outPixels(static_cast<size_t>(outW * outH * 3));
        auto *ok = stbir_resize_uint8_linear(img, w, h, 0, outPixels.data(), outW, outH, 0, STBIR_RGB);
        stbi_image_free(img);

        if (!ok)
        {
            return "Error: resize failed.";
        }

        // ensure parent directory exists
        std::filesystem::path p(outResolved);

        if (!p.parent_path().empty() && !std::filesystem::exists(p.parent_path()))
        {
            std::error_code ec;
            std::filesystem::create_directories(p.parent_path(), ec);
        }

        // write output image
        if (stbi_write_png(outResolved.c_str(), outW, outH, 3, outPixels.data(), 0) == 0)
        {
            return "Error: failed to write resized image to " + outputPath;
        }

        return "Image resized: " + outputPath;
    }

    // draw rectangle operation
    if (op == "draw_rect")
    {
        std::string inputPath = params.value("input_path", "");
        std::string outputPath = params.value("output_path", "");
        int x = params.value("x", 0);
        int y = params.value("y", 0);
        int rw = params.value("width", 0);
        int rh = params.value("height", 0);
        std::string colorHex = params.value("color", "000000");

        if (inputPath.empty() || outputPath.empty() || rw < 1 || rh < 1)
        {
            return "Error: draw_rect requires input_path, output_path, x, y, width, height, color (RRGGBB).";
        }

        // parse fill color
        unsigned char cr = 0, cg = 0, cb = 0;

        if (!parseHexColor(colorHex, cr, cg, cb))
        {
            return "Error: color must be hex RRGGBB.";
        }

        // resolve paths
        std::string inResolved = resolve(inputPath);
        std::string outResolved = resolve(outputPath);

        if (inResolved.empty() || outResolved.empty())
        {
            return "Error: input_path or output_path outside workspace or public.";
        }

        // load source image
        int w = 0, h = 0, ch = 0;
        unsigned char *img = stbi_load(inResolved.c_str(), &w, &h, &ch, 3);

        if (!img)
        {
            return "Error: could not load image: " + inputPath;
        }

        // fill rectangle region
        int x1 = std::max(0, x);
        int y1 = std::max(0, y);
        int x2 = std::min(w, x + rw);
        int y2 = std::min(h, y + rh);

        for (int py = y1; py < y2; ++py)
        {
            for (int px = x1; px < x2; ++px)
            {
                size_t i = static_cast<size_t>((py * w + px) * 3);
                img[i + 0] = cr;
                img[i + 1] = cg;
                img[i + 2] = cb;
            }
        }

        // write output image
        if (stbi_write_png(outResolved.c_str(), w, h, 3, img, 0) == 0)
        {
            stbi_image_free(img);
            return "Error: failed to write image to " + outputPath;
        }

        stbi_image_free(img);
        return "Rectangle drawn: " + outputPath;
    }

    // overlay operation
    if (op == "overlay")
    {
        std::string inputPath = params.value("input_path", "");
        std::string overlayPath = params.value("overlay_path", "");
        std::string outputPath = params.value("output_path", "");
        int x = params.value("x", 0);
        int y = params.value("y", 0);
        int overlayW = params.value("overlay_width", 0);
        int overlayH = params.value("overlay_height", 0);

        if (inputPath.empty() || overlayPath.empty() || outputPath.empty())
        {
            return "Error: overlay requires input_path, overlay_path, output_path, x, y.";
        }

        // resolve all paths
        std::string inResolved = resolve(inputPath);
        std::string ovResolved = resolve(overlayPath);
        std::string outResolved = resolve(outputPath);

        if (inResolved.empty() || ovResolved.empty() || outResolved.empty())
        {
            return "Error: paths must be within workspace or public.";
        }

        // load base image
        int w = 0, h = 0, ch = 0;
        unsigned char *base = stbi_load(inResolved.c_str(), &w, &h, &ch, 3);

        if (!base)
        {
            return "Error: could not load base image: " + inputPath;
        }

        // load overlay image
        int ow = 0, oh = 0, och = 0;
        unsigned char *over = stbi_load(ovResolved.c_str(), &ow, &oh, &och, 3);

        if (!over)
        {
            stbi_image_free(base);
            return "Error: could not load overlay image: " + overlayPath;
        }

        // optionally resize the overlay
        int pasteW = ow;
        int pasteH = oh;
        unsigned char *pasteSrc = over;
        std::vector<unsigned char> resizedOverlay;

        if (overlayW > 0 && overlayH > 0)
        {
            resizedOverlay.resize(static_cast<size_t>(overlayW * overlayH * 3));

            if (stbir_resize_uint8_linear(over, ow, oh, 0, resizedOverlay.data(), overlayW, overlayH, 0, STBIR_RGB) != nullptr)
            {
                pasteW = overlayW;
                pasteH = overlayH;
                pasteSrc = resizedOverlay.data();
            }
        }

        // blend overlay onto base
        int px = std::max(0, x);
        int py = std::max(0, y);
        int blendW = std::min(pasteW, w - px);
        int blendH = std::min(pasteH, h - py);

        if (blendW > 0 && blendH > 0)
        {
            for (int dy = 0; dy < blendH; ++dy)
            {
                for (int dx = 0; dx < blendW; ++dx)
                {
                    int dstIdx = ((py + dy) * w + (px + dx)) * 3;
                    int srcIdx = (dy * pasteW + dx) * 3;
                    base[dstIdx + 0] = pasteSrc[srcIdx + 0];
                    base[dstIdx + 1] = pasteSrc[srcIdx + 1];
                    base[dstIdx + 2] = pasteSrc[srcIdx + 2];
                }
            }
        }

        // write output image
        if (stbi_write_png(outResolved.c_str(), w, h, 3, base, 0) == 0)
        {
            stbi_image_free(base);
            stbi_image_free(over);
            return "Error: failed to write image to " + outputPath;
        }

        stbi_image_free(base);
        stbi_image_free(over);
        return "Overlay applied: " + outputPath;
    }

    return "Error: unknown operation '" + op + "'. Use create, resize, draw_rect, or overlay.";
#endif
}

ToolSchema ImageOpsTool::schema() const
{
    return {
        "image_ops",
        "Local image operations (no AI): create images, resize, draw rectangles, overlay/watermark. Use for programmatic image creation and editing. Paths can be under workspace or public (e.g. public/media/out.png).",
        {{"type", "object"},
         {"properties",
          {{"operation", {{"type", "string"}, {"enum", nlohmann::json::array({"create", "resize", "draw_rect", "overlay"})}, {"description", "create: new image. resize: change dimensions. draw_rect: fill rectangle with color. overlay: paste image on top at (x,y)."}}},
           {"output_path", {{"type", "string"}, {"description", "Output path (e.g. public/media/result.png)"}}},
           {"input_path", {{"type", "string"}, {"description", "Input image path (resize, draw_rect, overlay)"}}},
           {"overlay_path", {{"type", "string"}, {"description", "Overlay/watermark image path (overlay only)"}}},
           {"width", {{"type", "integer"}, {"description", "Width (create: image width; resize: target width; draw_rect: rect width)"}}},
           {"height", {{"type", "integer"}, {"description", "Height (create: image height; resize: target height; draw_rect: rect height)"}}},
           {"x", {{"type", "integer"}, {"description", "X position (draw_rect, overlay)"}}},
           {"y", {{"type", "integer"}, {"description", "Y position (draw_rect, overlay)"}}},
           {"overlay_width", {{"type", "integer"}, {"description", "overlay: width to resize overlay to before pasting (optional)"}}},
           {"overlay_height", {{"type", "integer"}, {"description", "overlay: height to resize overlay to before pasting (optional)"}}},
           {"background", {{"type", "string"}, {"enum", nlohmann::json::array({"gradient", "solid"})}, {"description", "create: gradient or solid color"}}},
           {"color", {{"type", "string"}, {"description", "Hex color RRGGBB (create with solid background; draw_rect fill color)"}}}}},
         {"required", nlohmann::json::array({"operation", "output_path"})}}};
}

} // namespace builtin
} // namespace tool
} // namespace ionclaw
