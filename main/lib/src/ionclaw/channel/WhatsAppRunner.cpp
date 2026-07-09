#include "ionclaw/channel/WhatsAppRunner.hpp"

#include "spdlog/spdlog.h"

#include "ionclaw/bus/Events.hpp"
#include "ionclaw/bus/MessageBus.hpp"
#include "ionclaw/channel/DeliveryParser.hpp"
#include "ionclaw/channel/WhatsAppSender.hpp"
#include "ionclaw/config/Config.hpp"
#include "ionclaw/util/HttpClient.hpp"

namespace ionclaw
{
namespace channel
{

static constexpr int OUTBOUND_POLL_MS = 500;
static constexpr size_t MAX_WHATSAPP_CHARS = 4000;

WhatsAppRunner::WhatsAppRunner(std::shared_ptr<const ionclaw::config::Config> config, std::shared_ptr<ionclaw::bus::MessageBus> bus)
    : config(std::move(config))
    , bus(std::move(bus))
{
}

WhatsAppRunner::~WhatsAppRunner()
{
    stop();
}

void WhatsAppRunner::start()
{
    if (running.exchange(true))
    {
        return;
    }

    outboundThread = std::thread(&WhatsAppRunner::outboundLoop, this);
    spdlog::info("[WhatsAppRunner] started");
}

void WhatsAppRunner::stop()
{
    if (!running.exchange(false))
    {
        return;
    }

    if (outboundThread.joinable())
    {
        outboundThread.join();
    }

    spdlog::info("[WhatsAppRunner] stopped");
}

std::vector<std::string> WhatsAppRunner::splitMessage(const std::string &text, size_t maxChars)
{
    std::vector<std::string> chunks;

    if (text.size() <= maxChars)
    {
        chunks.push_back(text);
        return chunks;
    }

    size_t pos = 0;

    while (pos < text.size())
    {
        size_t end = std::min(pos + maxChars, text.size());

        // back up so the cut never lands inside a utf-8 multi-byte sequence
        while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
        {
            --end;
        }

        chunks.push_back(text.substr(pos, end - pos));
        pos = end;
    }

    return chunks;
}

void WhatsAppRunner::outboundLoop()
{
    while (running.load())
    {
        try
        {
            ionclaw::bus::OutboundMessage outbound;

            if (!bus->consumeOutbound("whatsapp", outbound, OUTBOUND_POLL_MS))
            {
                continue;
            }

            send(outbound);
        }
        catch (const std::exception &e)
        {
            spdlog::error("[WhatsAppRunner] outbound error: {}", e.what());
        }
    }
}

std::string WhatsAppRunner::baseName(const std::string &path)
{
    auto pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

std::string WhatsAppRunner::resolvePublicUrl(const std::string &path) const
{
    // an absolute url is already reachable and passes through unchanged
    if (path.rfind("http://", 0) == 0 || path.rfind("https://", 0) == 0)
    {
        return path;
    }

    // media is served under the /public path, so it needs the configured public base url
    const auto &base = config->server.publicUrl;

    if (base.empty())
    {
        return "";
    }

    // normalize the marker path to a location under public/
    std::string rel = path;

    if (rel.rfind("public/", 0) == 0)
    {
        rel = rel.substr(std::string("public/").size());
    }

    while (!rel.empty() && rel.front() == '/')
    {
        rel = rel.substr(1);
    }

    return base + "/public/" + rel;
}

void WhatsAppRunner::execute(const OutboundRequest &req, const std::string &chatId)
{
    auto resp = ionclaw::util::HttpClient::request(req.method, req.url, req.headers, req.body, 30);

    if (resp.statusCode < 200 || resp.statusCode >= 300)
    {
        spdlog::error("[WhatsAppRunner] send to {} failed: HTTP {} {}", chatId, resp.statusCode, resp.body);
    }
}

void WhatsAppRunner::send(const ionclaw::bus::OutboundMessage &msg)
{
    if (msg.content.empty() || msg.chatId.empty())
    {
        return;
    }

    // resolve the active provider, meta taking precedence when both are configured
    auto metaIt = config->channels.find("whatsapp_meta");
    bool useMeta = metaIt != config->channels.end() && metaIt->second.enabled && metaIt->second.raw.is_object();

    auto zapiIt = config->channels.find("whatsapp_zapi");
    bool useZApi = zapiIt != config->channels.end() && zapiIt->second.enabled && zapiIt->second.raw.is_object();

    if (!useMeta && !useZApi)
    {
        spdlog::warn("[WhatsAppRunner] no whatsapp provider is enabled, dropping reply to {}", msg.chatId);
        return;
    }

    // clang-format off
    auto zApiBaseUrl = [&]() {
        const auto &raw = zapiIt->second.raw;
        return "https://api.z-api.io/instances/" + raw.value("instance_id", "") + "/token/" + raw.value("instance_token", "");
    };
    // clang-format on

    // the reply is split into deliverable parts: plain text messages and media attachments with optional captions
    for (const auto &part : ionclaw::channel::DeliveryParser::parse(msg.content))
    {
        if (part.mediaPath.empty())
        {
            // long text is split so each message stays under the whatsapp length limit
            for (const auto &chunk : splitMessage(part.text, MAX_WHATSAPP_CHARS))
            {
                if (useMeta)
                {
                    const auto &raw = metaIt->second.raw;
                    execute(WhatsAppSender::metaSendText(raw.value("graph_version", "v23.0"), raw.value("phone_number_id", ""), raw.value("access_token", ""), msg.chatId, chunk), msg.chatId);
                }
                else
                {
                    const auto &raw = zapiIt->second.raw;
                    execute(WhatsAppSender::zApiSendText(zApiBaseUrl(), raw.value("client_token", ""), msg.chatId, chunk), msg.chatId);
                }
            }

            continue;
        }

        // both providers fetch media by url, so the public url built from the workspace path is enough
        auto url = resolvePublicUrl(part.mediaPath);

        if (url.empty())
        {
            spdlog::error("[WhatsAppRunner] cannot send media '{}' to {}: server.public_url is not configured", part.mediaPath, msg.chatId);
            continue;
        }

        auto fileName = baseName(part.mediaPath);

        if (useMeta)
        {
            const auto &raw = metaIt->second.raw;
            execute(WhatsAppSender::metaSendMedia(raw.value("graph_version", "v23.0"), raw.value("phone_number_id", ""), raw.value("access_token", ""), msg.chatId, part.mediaKind, url, part.text, fileName), msg.chatId);
        }
        else
        {
            const auto &raw = zapiIt->second.raw;
            execute(WhatsAppSender::zApiSendMedia(zApiBaseUrl(), raw.value("client_token", ""), msg.chatId, part.mediaKind, url, part.text, fileName), msg.chatId);
        }
    }
}

} // namespace channel
} // namespace ionclaw
