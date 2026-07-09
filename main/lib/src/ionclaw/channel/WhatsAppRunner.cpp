#include "ionclaw/channel/WhatsAppRunner.hpp"

#include "spdlog/spdlog.h"

#include "ionclaw/bus/Events.hpp"
#include "ionclaw/bus/MessageBus.hpp"
#include "ionclaw/channel/WhatsAppSender.hpp"
#include "ionclaw/config/Config.hpp"
#include "ionclaw/util/HttpClient.hpp"

namespace ionclaw
{
namespace channel
{

static constexpr int OUTBOUND_POLL_MS = 500;
static constexpr size_t MAX_WHATSAPP_CHARS = 4000;

WhatsAppRunner::WhatsAppRunner(std::shared_ptr<ionclaw::config::Config> config, std::shared_ptr<ionclaw::bus::MessageBus> bus)
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

    // long replies are split so each stays under the whatsapp message limit
    auto chunks = splitMessage(msg.content, MAX_WHATSAPP_CHARS);

    for (const auto &chunk : chunks)
    {
        OutboundRequest req;

        if (useMeta)
        {
            const auto &raw = metaIt->second.raw;
            auto version = raw.value("graph_version", "v23.0");
            req = WhatsAppSender::metaSendText(version, raw.value("phone_number_id", ""), raw.value("access_token", ""), msg.chatId, chunk);
        }
        else
        {
            const auto &raw = zapiIt->second.raw;
            auto baseUrl = "https://api.z-api.io/instances/" + raw.value("instance_id", "") + "/token/" + raw.value("instance_token", "");
            req = WhatsAppSender::zApiSendText(baseUrl, raw.value("client_token", ""), msg.chatId, chunk);
        }

        auto resp = ionclaw::util::HttpClient::request(req.method, req.url, req.headers, req.body, 30);

        if (resp.statusCode < 200 || resp.statusCode >= 300)
        {
            spdlog::error("[WhatsAppRunner] send to {} failed: HTTP {} {}", msg.chatId, resp.statusCode, resp.body);
        }
    }
}

} // namespace channel
} // namespace ionclaw
