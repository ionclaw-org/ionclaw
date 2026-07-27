#include "ionclaw/server/Routes.hpp"

#include <algorithm>
#include <vector>

#include "Poco/URI.h"
#include "spdlog/spdlog.h"

#include "ionclaw/channel/WhatsAppWebhook.hpp"
#include "ionclaw/session/SessionManager.hpp"
#include "ionclaw/util/FileHelper.hpp"
#include "ionclaw/util/HttpClient.hpp"
#include "ionclaw/util/StringHelper.hpp"
#include "ionclaw/util/TimeHelper.hpp"
#include "ionclaw/util/UniqueId.hpp"

namespace ionclaw
{
namespace server
{

void Routes::downloadWhatsAppMedia(ionclaw::channel::WebhookMedia &media, const std::string &accessToken, const std::string &graphVersion)
{
    std::string url = media.url;
    std::map<std::string, std::string> headers;

    // meta media: resolve the id to a temporary url; both this and the download use the bearer token
    if (url.empty() && !media.mediaId.empty() && !accessToken.empty())
    {
        headers["Authorization"] = "Bearer " + accessToken;
        auto info = ionclaw::util::HttpClient::request("GET", "https://graph.facebook.com/" + graphVersion + "/" + media.mediaId, headers, "", 30);

        if (info.statusCode < 200 || info.statusCode >= 300)
        {
            spdlog::error("[Routes] Whatsapp meta media info failed: HTTP {}", info.statusCode);
            return;
        }

        try
        {
            auto j = nlohmann::json::parse(info.body);
            url = j.value("url", "");

            if (media.mimeType.empty())
            {
                media.mimeType = j.value("mime_type", "");
            }
        }
        catch (const std::exception &e)
        {
            spdlog::error("[Routes] Whatsapp meta media info parse failed: {}", e.what());
            return;
        }
    }

    if (url.empty())
    {
        return;
    }

    auto resp = ionclaw::util::HttpClient::request("GET", url, headers, "", 60);

    if (resp.statusCode < 200 || resp.statusCode >= 300 || resp.body.empty())
    {
        spdlog::error("[Routes] Whatsapp media download failed: HTTP {}", resp.statusCode);
        return;
    }

    // extension from the original file name, else the mime type
    std::string ext;
    auto dot = media.fileName.find_last_of('.');

    if (dot != std::string::npos)
    {
        ext = media.fileName.substr(dot);
    }
    else
    {
        const auto &m = media.mimeType;
        ext = m == "image/jpeg" ? ".jpg" : m == "image/png" ? ".png" : m == "image/webp" ? ".webp" : m == "audio/ogg" ? ".oga" : m == "audio/mpeg" ? ".mp3" : (m == "audio/mp4" || m == "audio/aac") ? ".m4a" : m == "video/mp4" ? ".mp4" : m == "application/pdf" ? ".pdf" : ".bin";
    }

    auto name = ionclaw::util::UniqueId::shortId() + ext;
    auto error = ionclaw::util::FileHelper::atomicWrite(publicDir + "/media/" + name, resp.body);

    if (!error.empty())
    {
        spdlog::error("[Routes] Whatsapp media save failed: {}", error);
        return;
    }

    media.localPath = "public/media/" + name;
}

// builds and publishes an inbound whatsapp message, mirroring the chat-send path
void Routes::publishWhatsAppInbound(const ionclaw::channel::ParsedWebhookMessage &msg)
{
    auto sessionKey = std::string("whatsapp:") + msg.senderPhone;
    sessionManager->ensureSession(sessionKey);

    // the caption of a media-only message stands in for the text
    std::string content = msg.text;
    std::vector<std::string> mediaPaths;

    for (const auto &m : msg.media)
    {
        if (!m.localPath.empty())
        {
            mediaPaths.push_back(m.localPath);
        }

        if (content.empty() && !m.caption.empty())
        {
            content = m.caption;
        }
    }

    auto title = content.empty() ? "[media]" : ionclaw::util::StringHelper::utf8SafeTruncate(content, 100);
    auto task = taskManager->createTask(title, content, "whatsapp", msg.senderPhone);

    ionclaw::session::SessionMessage userMsg;
    userMsg.role = "user";
    userMsg.content = content;

    for (const auto &p : mediaPaths)
    {
        userMsg.media.push_back(p);
    }

    userMsg.timestamp = ionclaw::util::TimeHelper::now();
    sessionManager->addMessage(sessionKey, userMsg);

    ionclaw::bus::InboundMessage inbound;
    inbound.channel = "whatsapp";
    inbound.senderId = msg.senderPhone;
    inbound.chatId = msg.senderPhone;
    inbound.content = content;
    inbound.media = mediaPaths;
    inbound.metadata = {{"task_id", task.id}, {"message_saved", true}, {"sender_name", msg.senderName}, {"provider_message_id", msg.msgId}};

    bus->publishInbound(inbound);
}

void Routes::handleWhatsAppZApiWebhook(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp)
{
    // z-api only posts inbound messages; a stray get is answered so the provider stops retrying
    if (req.getMethod() != "POST")
    {
        sendJson(resp, {{"status", "ok"}});
        return;
    }

    bool enabled = false;
    std::vector<std::string> allowedUsers;
    {
        auto config = configStore->snapshot();
        auto it = config->channels.find("whatsapp_zapi");

        if (it != config->channels.end() && it->second.enabled)
        {
            enabled = true;
            allowedUsers = it->second.allowedUsers;
        }
    }

    if (!enabled)
    {
        resp.setStatus(Poco::Net::HTTPResponse::HTTP_NOT_FOUND);
        sendJson(resp, {{"error", "z-api channel is not enabled"}});
        return;
    }

    try
    {
        auto body = nlohmann::json::parse(readBody(req));
        auto msg = ionclaw::channel::WhatsAppWebhook::parseZApi(body);

        // ignore our own echoes, groups, and anything without a real payload
        if (!msg.valid || msg.fromMe || msg.isGroup)
        {
            sendJson(resp, {{"status", "ignored"}});
            return;
        }

        // when an allowlist is configured, only its numbers may talk to the agent
        if (!allowedUsers.empty() && std::find(allowedUsers.begin(), allowedUsers.end(), msg.senderPhone) == allowedUsers.end())
        {
            sendJson(resp, {{"status", "ignored"}});
            return;
        }

        if (!webhookDedup.markSeen(msg.msgId))
        {
            sendJson(resp, {{"status", "duplicate"}});
            return;
        }

        // z-api media is a public url, downloaded with no auth
        for (auto &media : msg.media)
        {
            downloadWhatsAppMedia(media, "", "");
        }

        publishWhatsAppInbound(msg);
        sendJson(resp, {{"status", "ok"}});
    }
    catch (const std::exception &e)
    {
        spdlog::error("[Routes] Z-api webhook error: {}", e.what());
        sendError(resp, e.what(), 500);
    }
}

void Routes::handleWhatsAppMetaWebhook(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp)
{
    std::string verifyToken;
    std::string appSecret;
    std::string accessToken;
    std::string graphVersion = "v23.0";
    std::vector<std::string> allowedUsers;
    bool enabled = false;
    {
        auto config = configStore->snapshot();
        auto it = config->channels.find("whatsapp_meta");

        if (it != config->channels.end() && it->second.enabled && it->second.raw.is_object())
        {
            enabled = true;
            verifyToken = it->second.raw.value("verify_token", "");
            appSecret = it->second.raw.value("app_secret", "");
            accessToken = it->second.raw.value("access_token", "");
            graphVersion = it->second.raw.value("graph_version", "v23.0");
            allowedUsers = it->second.allowedUsers;
        }
    }

    if (!enabled)
    {
        resp.setStatus(Poco::Net::HTTPResponse::HTTP_NOT_FOUND);
        sendJson(resp, {{"error", "whatsapp meta channel is not enabled"}});
        return;
    }

    // get: subscription verification handshake, echo the challenge back as plain text
    if (req.getMethod() == "GET")
    {
        Poco::URI uri(req.getURI());
        std::string mode, token, challenge;

        for (const auto &[key, value] : uri.getQueryParameters())
        {
            if (key == "hub.mode")
                mode = value;
            else if (key == "hub.verify_token")
                token = value;
            else if (key == "hub.challenge")
                challenge = value;
        }

        if (mode == "subscribe" && !verifyToken.empty() && token == verifyToken)
        {
            resp.setStatus(Poco::Net::HTTPResponse::HTTP_OK);
            resp.setContentType("text/plain");
            auto &out = resp.send();
            out << challenge;
            return;
        }

        resp.setStatus(Poco::Net::HTTPResponse::HTTP_FORBIDDEN);
        resp.setContentType("text/plain");
        auto &out = resp.send();
        out << "Forbidden";
        return;
    }

    if (req.getMethod() != "POST")
    {
        sendJson(resp, {{"status", "ok"}});
        return;
    }

    try
    {
        auto rawBody = readBody(req);

        // validate the meta signature over the exact raw bytes before trusting the payload
        auto signature = req.get("X-Hub-Signature-256", "");

        if (!ionclaw::channel::WhatsAppWebhook::verifyMetaSignature(rawBody, appSecret, signature))
        {
            spdlog::warn("[Routes] Whatsapp meta webhook signature verification failed");
            resp.setStatus(Poco::Net::HTTPResponse::HTTP_FORBIDDEN);
            sendJson(resp, {{"error", "invalid signature"}});
            return;
        }

        auto body = nlohmann::json::parse(rawBody);

        for (auto msg : ionclaw::channel::WhatsAppWebhook::parseMeta(body))
        {
            if (!webhookDedup.markSeen(msg.msgId))
            {
                continue;
            }

            // when an allowlist is configured, only its numbers may talk to the agent
            if (!allowedUsers.empty() && std::find(allowedUsers.begin(), allowedUsers.end(), msg.senderPhone) == allowedUsers.end())
            {
                continue;
            }

            // meta media is fetched in two authenticated steps by id
            for (auto &media : msg.media)
            {
                downloadWhatsAppMedia(media, accessToken, graphVersion);
            }

            publishWhatsAppInbound(msg);
        }

        sendJson(resp, {{"status", "ok"}});
    }
    catch (const std::exception &e)
    {
        spdlog::error("[Routes] Whatsapp meta webhook error: {}", e.what());
        sendError(resp, e.what(), 500);
    }
}

} // namespace server
} // namespace ionclaw
