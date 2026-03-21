#include "ionclaw/tool/builtin/VisionTool.hpp"

#include <filesystem>

#include "ionclaw/config/Config.hpp"
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "stb_image.h"
#include "stb_image_resize2.h"
#include "stb_image_write.h"

#include "spdlog/spdlog.h"

#include "ionclaw/tool/builtin/ToolHelper.hpp"
#include "ionclaw/util/Base64.hpp"
#include "ionclaw/util/HttpClient.hpp"
#include "ionclaw/util/SsrfGuard.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

namespace
{

// max raw image size: 20MB
constexpr size_t MAX_IMAGE_BYTES = 20 * 1024 * 1024;

// preview constraints (same as BrowserTool screenshot preview)
constexpr int MAX_PREVIEW_WIDTH = 1024;
constexpr int PREVIEW_JPEG_QUALITY = 50;

// non-image extensions that should be rejected by the vision tool
const std::set<std::string> NON_IMAGE_EXTENSIONS = {
    ".mp3",
    ".wav",
    ".ogg",
    ".oga",
    ".opus",
    ".m4a",
    ".webm",
    ".aac",
    ".flac",
    ".mp4",
    ".avi",
    ".mkv",
    ".mov",
    ".pdf",
    ".doc",
    ".docx",
    ".xls",
    ".xlsx",
    ".ppt",
    ".pptx",
    ".txt",
    ".md",
    ".csv",
    ".json",
    ".xml",
    ".yaml",
    ".yml",
    ".zip",
    ".tar",
    ".gz",
    ".rar",
    ".py",
    ".js",
    ".ts",
    ".cpp",
    ".hpp",
    ".h",
    ".c",
    ".java",
    ".rs",
};

std::string normalizeExtension(const std::string &pathOrUrl)
{
    auto clean = pathOrUrl;
    auto queryPos = clean.find('?');

    if (queryPos != std::string::npos)
    {
        clean = clean.substr(0, queryPos);
    }

    auto dotPos = clean.rfind('.');

    if (dotPos == std::string::npos)
    {
        return "";
    }

    auto ext = clean.substr(dotPos);

    for (auto &c : ext)
    {
        if (c >= 'A' && c <= 'Z')
        {
            c = static_cast<char>(c + ('a' - 'A'));
        }
    }

    return ext;
}

std::string detectMimeType(const std::string &pathOrUrl)
{
    auto ext = normalizeExtension(pathOrUrl);

    if (ext.empty())
    {
        return "image/png"; // safe default for extensionless paths
    }

    if (ext == ".jpg" || ext == ".jpeg")
        return "image/jpeg";
    if (ext == ".png")
        return "image/png";
    if (ext == ".gif")
        return "image/gif";
    if (ext == ".webp")
        return "image/webp";
    if (ext == ".svg")
        return "image/svg+xml";
    if (ext == ".bmp")
        return "image/bmp";
    if (ext == ".ico")
        return "image/x-icon";
    if (ext == ".tiff" || ext == ".tif")
        return "image/tiff";
    if (ext == ".avif")
        return "image/avif";

    return "image/png";
}

std::string detectMimeFromContentType(const std::string &contentType)
{
    // extract mime type from Content-Type header (strip charset etc.)
    auto pos = contentType.find(';');
    auto mime = (pos != std::string::npos) ? contentType.substr(0, pos) : contentType;

    // trim whitespace
    while (!mime.empty() && mime.front() == ' ')
        mime.erase(mime.begin());
    while (!mime.empty() && mime.back() == ' ')
        mime.pop_back();

    if (mime.find("image/") == 0)
    {
        return mime;
    }

    return "";
}

// stb callback that appends to a vector<unsigned char>
void stbWriteToVector(void *context, void *data, int size)
{
    auto *vec = static_cast<std::vector<unsigned char> *>(context);
    auto *bytes = static_cast<unsigned char *>(data);
    vec->insert(vec->end(), bytes, bytes + size);
}

} // anonymous namespace

ToolResult VisionTool::execute(const nlohmann::json &params, const ToolContext &context)
{
    auto path = params.value("path", "");
    auto url = params.value("url", "");
    auto base64Input = params.value("base64", "");
    auto question = params.value("question", "");

    spdlog::info("vision: execute (path={}, url={}, base64_len={}, question={})",
                 path.empty() ? "(none)" : path,
                 url.empty() ? "(none)" : url,
                 base64Input.size(),
                 question.empty() ? "(none)" : question);

    // exactly one source required
    int sources = (!path.empty() ? 1 : 0) + (!url.empty() ? 1 : 0) + (!base64Input.empty() ? 1 : 0);

    if (sources == 0)
    {
        spdlog::warn("vision: no image source provided");
        return "Error: you must provide one of 'path', 'url', or 'base64'. "
               "For user-uploaded images, use the path from the [image attached: path] annotation.";
    }

    if (sources > 1)
    {
        spdlog::warn("vision: multiple image sources provided");
        return "Error: provide only one image source — 'path', 'url', or 'base64' — not multiple.";
    }

    std::string rawBytes; // decoded image bytes
    std::string mimeType;
    std::string sourceLabel; // for logging and result text

    // ── source: local file ────────────────────────────────────────────────
    if (!path.empty())
    {
        sourceLabel = path;

        // resolve relative paths against workspace/public root
        try
        {
            bool restrict = !context.config || context.config->tools.restrictToWorkspace;
            path = ToolHelper::validateAndResolvePath(context.projectPath, context.workspacePath, path, context.publicPath, restrict);
            spdlog::info("vision: resolved path to: {}", path);
        }
        catch (const std::exception &e)
        {
            spdlog::warn("vision: path resolution failed: {}", e.what());
            return "Error: " + std::string(e.what());
        }

        std::error_code ec;

        if (!std::filesystem::exists(path, ec) || ec)
        {
            spdlog::warn("vision: file not found: {} ({})", path, ec ? ec.message() : "");
            return "Error: file not found" + (ec ? " (" + ec.message() + ")" : "") + ": " + path;
        }

        if (!std::filesystem::is_regular_file(path, ec) || ec)
        {
            spdlog::warn("vision: not a regular file: {} ({})", path, ec ? ec.message() : "");
            return "Error: not a regular file" + (ec ? " (" + ec.message() + ")" : "") + ": " + path;
        }

        // reject non-image files (audio, video, documents, code, etc.)
        auto fileExt = normalizeExtension(path);

        if (NON_IMAGE_EXTENSIONS.count(fileExt) > 0)
        {
            return "Error: '" + path + "' is not an image file (" + fileExt + "). "
                                                                              "The vision tool only processes images. "
                                                                              "For audio files, the transcription is already included in the conversation.";
        }

        auto fileSize = std::filesystem::file_size(path, ec);

        if (ec)
        {
            spdlog::warn("vision: cannot read file size: {} ({})", path, ec.message());
            return "Error: cannot read file size (" + ec.message() + "): " + path;
        }

        if (fileSize == 0)
        {
            spdlog::warn("vision: file is empty: {}", path);
            return "Error: file is empty: " + path;
        }

        if (fileSize > MAX_IMAGE_BYTES)
        {
            spdlog::warn("vision: file too large: {} ({}MB)", path, fileSize / (1024 * 1024));
            return "Error: file too large (" + std::to_string(fileSize / (1024 * 1024)) +
                   "MB, max " + std::to_string(MAX_IMAGE_BYTES / (1024 * 1024)) + "MB): " + path;
        }

        // read raw bytes
        std::ifstream file(path, std::ios::binary);

        if (!file.good())
        {
            spdlog::error("vision: failed to open file: {}", path);
            return "Error: failed to open file: " + path;
        }

        rawBytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());

        if (rawBytes.empty())
        {
            spdlog::error("vision: failed to read file contents: {}", path);
            return "Error: failed to read file: " + path;
        }

        mimeType = detectMimeType(path);

        auto userMime = params.value("mime_type", "");

        if (!userMime.empty())
        {
            mimeType = userMime;
        }

        spdlog::debug("vision: loaded file {} ({}KB, {})", path, rawBytes.size() / 1024, mimeType);
    }

    // ── source: remote URL ────────────────────────────────────────────────
    if (!url.empty())
    {
        sourceLabel = url;
        spdlog::debug("vision: fetching URL: {}", url);

        // ssrf validation
        try
        {
            ionclaw::util::SsrfGuard::validateUrl(url);
        }
        catch (const std::exception &e)
        {
            spdlog::warn("vision: SSRF blocked URL: {} ({})", url, e.what());
            return "Error: " + std::string(e.what());
        }

        try
        {
            auto response = ionclaw::util::HttpClient::request(
                "GET", url, {}, "", 30, true,
                ionclaw::util::SsrfGuard::validateUrl);

            if (response.statusCode != 200)
            {
                spdlog::warn("vision: URL returned HTTP {}: {}", response.statusCode, url);
                return "Error: failed to fetch image from URL (HTTP " +
                       std::to_string(response.statusCode) + "): " + url;
            }

            if (response.body.empty())
            {
                spdlog::warn("vision: URL returned empty body: {}", url);
                return "Error: URL returned empty body: " + url;
            }

            if (response.body.size() > MAX_IMAGE_BYTES)
            {
                spdlog::warn("vision: image too large from URL: {} ({}MB)", url, response.body.size() / (1024 * 1024));
                return "Error: image too large (" +
                       std::to_string(response.body.size() / (1024 * 1024)) +
                       "MB, max " + std::to_string(MAX_IMAGE_BYTES / (1024 * 1024)) + "MB): " + url;
            }

            rawBytes = std::move(response.body);

            // detect mime from Content-Type header first, fallback to URL extension
            auto contentType = response.headers.count("Content-Type")
                                   ? response.headers.at("Content-Type")
                                   : (response.headers.count("content-type")
                                          ? response.headers.at("content-type")
                                          : "");

            mimeType = detectMimeFromContentType(contentType);

            if (mimeType.empty())
            {
                mimeType = detectMimeType(url);
            }

            auto userMime = params.value("mime_type", "");

            if (!userMime.empty())
            {
                mimeType = userMime;
            }

            spdlog::debug("vision: fetched URL {} ({}KB, {})", url, rawBytes.size() / 1024, mimeType);
        }
        catch (const std::exception &e)
        {
            spdlog::error("vision: failed to fetch URL {}: {}", url, e.what());
            return "Error: failed to fetch image from URL: " + std::string(e.what());
        }
    }

    // ── source: raw base64 ───────────────────────────────────────────────
    if (!base64Input.empty())
    {
        sourceLabel = "(base64 input)";
        std::string b64Data;

        // strip data URI prefix if present (e.g. "data:image/png;base64,...")
        auto commaPos = base64Input.find(',');

        if (commaPos != std::string::npos && base64Input.substr(0, 5) == "data:")
        {
            auto header = base64Input.substr(0, commaPos);
            b64Data = base64Input.substr(commaPos + 1);

            // extract mime from data URI header
            auto colonPos = header.find(':');
            auto semiPos = header.find(';');

            if (colonPos != std::string::npos && semiPos != std::string::npos && semiPos > colonPos)
            {
                mimeType = header.substr(colonPos + 1, semiPos - colonPos - 1);
            }
        }
        else
        {
            b64Data = base64Input;
        }

        if (mimeType.empty())
        {
            mimeType = params.value("mime_type", "image/png");
        }

        if (b64Data.empty())
        {
            spdlog::warn("vision: base64 data is empty after processing");
            return "Error: base64 data is empty after processing";
        }

        rawBytes = ionclaw::util::Base64::decode(b64Data);

        if (rawBytes.empty())
        {
            spdlog::warn("vision: failed to decode base64 data");
            return "Error: failed to decode base64 data";
        }

        if (rawBytes.size() > MAX_IMAGE_BYTES)
        {
            spdlog::warn("vision: decoded image too large ({}MB)", rawBytes.size() / (1024 * 1024));
            return "Error: image too large (" +
                   std::to_string(rawBytes.size() / (1024 * 1024)) +
                   "MB, max " + std::to_string(MAX_IMAGE_BYTES / (1024 * 1024)) + "MB)";
        }

        spdlog::debug("vision: decoded base64 input ({}KB, {})", rawBytes.size() / 1024, mimeType);
    }

    // ── generate LLM-friendly preview ─────────────────────────────────────
    // resize large images to prevent context overflow.
    // same approach as BrowserTool: max 1024px wide, jpeg quality 50.

    auto originalSizeKB = rawBytes.size() / 1024;

    int w = 0, h = 0, channels = 0;
    auto pixelsDeleter = [](unsigned char *p)
    { if (p) stbi_image_free(p); };
    std::unique_ptr<unsigned char, decltype(pixelsDeleter)> pixels(
        stbi_load_from_memory(
            reinterpret_cast<const unsigned char *>(rawBytes.data()),
            static_cast<int>(rawBytes.size()), &w, &h, &channels, 3),
        pixelsDeleter);

    if (!pixels)
    {
        // svg, ico, tiff etc. can't be rasterized by stb — send raw data for the model to process
        spdlog::warn("vision: stb_image could not decode image ({}), sending raw data", mimeType);

        auto b64 = ionclaw::util::Base64::encode(rawBytes);

        std::string description = "Image loaded (" + mimeType + ", " + std::to_string(originalSizeKB) + "KB). "
                                                                                                        "Format not rasterizable — sending original data.";

        if (!question.empty())
        {
            description += " Question: " + question;
        }

        ToolResult result;
        result.text = description;
        result.media = nlohmann::json::array({
            {{"type", "image"}, {"media_type", mimeType}, {"data", b64}},
        });
        return result;
    }

    spdlog::debug("vision: decoded pixels {}x{} ({} channels)", w, h, channels);

    // resize if wider than max preview width
    int previewW = w;
    int previewH = h;
    std::vector<unsigned char> resizedPixels;
    unsigned char *previewData = pixels.get();

    if (w > MAX_PREVIEW_WIDTH)
    {
        previewW = MAX_PREVIEW_WIDTH;
        previewH = static_cast<int>(static_cast<double>(h) * MAX_PREVIEW_WIDTH / w);

        if (previewH < 1)
            previewH = 1;

        resizedPixels.resize(static_cast<size_t>(previewW) * static_cast<size_t>(previewH) * 3);
        auto *ok = stbir_resize_uint8_linear(
            pixels.get(), w, h, 0,
            resizedPixels.data(), previewW, previewH, 0, STBIR_RGB);

        if (!ok)
        {
            spdlog::error("vision: resize failed for {}x{} -> {}x{}", w, h, previewW, previewH);
            return "Error: image resize failed (" + std::to_string(w) + "x" + std::to_string(h) +
                   " -> " + std::to_string(previewW) + "x" + std::to_string(previewH) + ")";
        }

        previewData = resizedPixels.data();
        spdlog::debug("vision: resized {}x{} -> {}x{}", w, h, previewW, previewH);
    }

    // encode preview as JPEG to memory
    std::vector<unsigned char> jpegBuf;
    jpegBuf.reserve(static_cast<size_t>(previewW * previewH));
    stbi_write_jpg_to_func(stbWriteToVector, &jpegBuf, previewW, previewH, 3, previewData, PREVIEW_JPEG_QUALITY);

    pixels.reset();

    if (jpegBuf.empty())
    {
        spdlog::error("vision: JPEG encode failed for {}x{}", previewW, previewH);
        return "Error: JPEG compression failed (" + std::to_string(previewW) + "x" + std::to_string(previewH) + ")";
    }

    auto previewB64 = ionclaw::util::Base64::encode(
        reinterpret_cast<const unsigned char *>(jpegBuf.data()), jpegBuf.size());
    auto previewKB = jpegBuf.size() / 1024;

    spdlog::info("vision: preview {}x{} ({}KB JPEG) from original {}x{} ({}KB {})",
                 previewW, previewH, previewKB, w, h, originalSizeKB, mimeType);

    std::string description = "Image analyzed (" + std::to_string(w) + "x" + std::to_string(h) + ", " +
                              std::to_string(originalSizeKB) + "KB " + mimeType + "). " +
                              "Preview: " + std::to_string(previewW) + "x" + std::to_string(previewH) +
                              " (" + std::to_string(previewKB) + "KB JPEG).";

    if (!question.empty())
    {
        description += " Question: " + question;
    }
    else
    {
        description += " Analyze and describe this image in detail.";
    }

    ToolResult result;
    result.text = description;
    result.media = nlohmann::json::array({
        {{"type", "image"}, {"media_type", "image/jpeg"}, {"data", previewB64}},
    });
    return result;
}

ToolSchema VisionTool::schema() const
{
    return {
        "vision",
        "Analyze images from local files, URLs, or base64 data. "
        "You MUST provide exactly one source parameter: 'path', 'url', or 'base64'. "
        "For user-uploaded images, the path is provided in the [image attached: path] annotation.",
        {{"type", "object"},
         {"properties",
          {
              {"path", {{"type", "string"}, {"description", "Local image file path (relative to workspace or absolute). For screenshots or files on disk."}}},
              {"url", {{"type", "string"}, {"description", "URL of a remote image to fetch and analyze."}}},
              {"base64", {{"type", "string"}, {"description", "Base64-encoded image data (with or without data URI prefix)."}}},
              {"question", {{"type", "string"}, {"description", "Specific question about the image. If omitted, provides a general description."}}},
              {"mime_type", {{"type", "string"}, {"description", "Override MIME type (e.g. image/png). Auto-detected if not specified."}}},
          }}}};
}

} // namespace builtin
} // namespace tool
} // namespace ionclaw
