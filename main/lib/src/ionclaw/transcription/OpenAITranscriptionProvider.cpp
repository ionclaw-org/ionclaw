#include "ionclaw/transcription/OpenAITranscriptionProvider.hpp"

#include "Poco/Net/HTMLForm.h"
#include "Poco/Net/HTTPClientSession.h"
#include "Poco/Net/HTTPRequest.h"
#include "Poco/Net/HTTPResponse.h"
#include "Poco/Net/PartSource.h"
#include "Poco/StreamCopier.h"
#include "Poco/URI.h"

#include "ionclaw/util/StringHelper.hpp"

#ifdef IONCLAW_HAS_SSL
#include "Poco/Net/Context.h"
#include "Poco/Net/HTTPSClientSession.h"
#endif

#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

#include "ionclaw/config/Config.hpp"

namespace ionclaw
{
namespace transcription
{

// in-memory part source for multipart form uploads
OpenAITranscriptionProvider::StringPartSource::StringPartSource(
    const std::string &data, const std::string &mediaType, const std::string &filename)
    : Poco::Net::PartSource(mediaType)
    , data_(data)
    , stream_(data_)
    , filename_(filename)
{
}

std::istream &OpenAITranscriptionProvider::StringPartSource::stream()
{
    return stream_;
}

const std::string &OpenAITranscriptionProvider::StringPartSource::filename() const
{
    return filename_;
}

OpenAITranscriptionProvider::OpenAITranscriptionProvider(const std::string &providerName)
    : providerName_(providerName)
{
}

std::string OpenAITranscriptionProvider::providerName() const
{
    return providerName_;
}

std::string OpenAITranscriptionProvider::audioMimeType(const std::string &format)
{
    if (format == "mp3")
    {
        return "audio/mpeg";
    }
    if (format == "wav")
    {
        return "audio/wav";
    }
    if (format == "ogg")
    {
        return "audio/ogg";
    }
    if (format == "m4a")
    {
        return "audio/mp4";
    }
    if (format == "webm")
    {
        return "audio/webm";
    }
    if (format == "aac")
    {
        return "audio/aac";
    }
    if (format == "flac")
    {
        return "audio/flac";
    }

    return "application/octet-stream";
}

std::string OpenAITranscriptionProvider::stripModelPrefix(const std::string &model)
{
    auto pos = model.find('/');

    if (pos != std::string::npos)
    {
        return model.substr(pos + 1);
    }

    return model;
}

TranscriptionResult OpenAITranscriptionProvider::transcribe(const std::string &audioData,
                                                            const std::string &format,
                                                            const TranscriptionContext &context) const
{
    if (!context.config)
    {
        spdlog::error("[transcription] no config available");
        return {};
    }

    auto apiKey = context.config->resolveApiKey(providerName_);

    if (apiKey.empty())
    {
        spdlog::error("[transcription] no API key for provider '{}'", providerName_);
        return {};
    }

    auto baseUrl = context.config->resolveBaseUrl(providerName_);
    auto modelId = stripModelPrefix(context.model);

    // build URL
    std::string urlStr = baseUrl + "/v1/audio/transcriptions";

    // ensure no double slashes between base URL and path
    if (!baseUrl.empty() && baseUrl.back() == '/')
    {
        urlStr = baseUrl + "v1/audio/transcriptions";
    }

    spdlog::info("[transcription] calling {} with model '{}' ({} bytes audio)", urlStr, modelId, audioData.size());

    Poco::URI uri(urlStr);
    auto host = uri.getHost();
    auto port = uri.getPort();
    auto path = uri.getPathAndQuery();

    if (path.empty())
    {
        path = "/";
    }

    // create session
    std::unique_ptr<Poco::Net::HTTPClientSession> session;

#ifdef IONCLAW_HAS_SSL
    if (uri.getScheme() == "https")
    {
#ifdef _WIN32
        Poco::Net::Context::Ptr ctx = new Poco::Net::Context(
            Poco::Net::Context::CLIENT_USE, "");
#else
        Poco::Net::Context::Ptr ctx = new Poco::Net::Context(
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

    // build request
    Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_POST, path, Poco::Net::HTTPMessage::HTTP_1_1);
    request.set("Host", host);
    request.set("Authorization", "Bearer " + apiKey);

    // build multipart form
    std::string filename = "audio." + format;
    auto mime = audioMimeType(format);

    Poco::Net::HTMLForm form;
    form.setEncoding(Poco::Net::HTMLForm::ENCODING_MULTIPART);
    form.set("model", modelId);
    form.set("response_format", "verbose_json");

    if (!context.language.empty())
    {
        form.set("language", context.language);
    }

    form.addPart("file", new StringPartSource(audioData, mime, filename));
    form.prepareSubmit(request);
    form.write(session->sendRequest(request));

    // read response
    Poco::Net::HTTPResponse httpResp;
    auto &rs = session->receiveResponse(httpResp);
    std::string respBody;
    Poco::StreamCopier::copyToString(rs, respBody);

    auto status = static_cast<int>(httpResp.getStatus());

    if (status != 200)
    {
        spdlog::error("[transcription] API returned HTTP {}: {}", status, ionclaw::util::StringHelper::utf8SafeTruncate(respBody, 500));
        return {};
    }

    // parse response
    try
    {
        auto json = nlohmann::json::parse(respBody);

        TranscriptionResult result;
        result.text = json.value("text", "");
        result.language = json.value("language", "");
        result.durationSeconds = json.value("duration", 0.0);

        spdlog::info("[transcription] transcribed {} seconds of audio (lang={}): {}",
                     result.durationSeconds, result.language,
                     ionclaw::util::StringHelper::utf8SafeTruncate(result.text, 100));

        return result;
    }
    catch (const std::exception &e)
    {
        spdlog::error("[transcription] failed to parse response: {}", e.what());
        return {};
    }
}

} // namespace transcription
} // namespace ionclaw
