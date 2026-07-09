#include "ionclaw/server/Routes.hpp"

#include "Poco/URI.h"
#include "spdlog/spdlog.h"

#include "ionclaw/channel/WhatsAppWebhook.hpp"
#include "ionclaw/session/SessionManager.hpp"
#include "ionclaw/util/StringHelper.hpp"
#include "ionclaw/util/TimeHelper.hpp"

namespace ionclaw
{
namespace server
{

// builds and publishes an inbound whatsapp message, mirroring the chat-send path
void Routes::publishWhatsAppInbound(const ionclaw::channel::ParsedWebhookMessage &msg)
{
    auto sessionKey = std::string("whatsapp:") + msg.senderPhone;
    sessionManager->ensureSession(sessionKey);

    auto title = msg.text.empty() ? "[media]" : ionclaw::util::StringHelper::utf8SafeTruncate(msg.text, 100);
    auto task = taskManager->createTask(title, msg.text, "whatsapp", msg.senderPhone);

    ionclaw::session::SessionMessage userMsg;
    userMsg.role = "user";
    userMsg.content = msg.text;
    userMsg.timestamp = ionclaw::util::TimeHelper::now();
    sessionManager->addMessage(sessionKey, userMsg);

    ionclaw::bus::InboundMessage inbound;
    inbound.channel = "whatsapp";
    inbound.senderId = msg.senderPhone;
    inbound.chatId = msg.senderPhone;
    inbound.content = msg.text;
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
    {
        std::lock_guard<std::mutex> lock(configMutex);
        auto it = config->channels.find("whatsapp_zapi");
        enabled = it != config->channels.end() && it->second.enabled;
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

        if (!webhookDedup.markSeen(msg.msgId))
        {
            sendJson(resp, {{"status", "duplicate"}});
            return;
        }

        publishWhatsAppInbound(msg);
        sendJson(resp, {{"status", "ok"}});
    }
    catch (const std::exception &e)
    {
        spdlog::error("[Routes] z-api webhook error: {}", e.what());
        sendError(resp, e.what(), 500);
    }
}

void Routes::handleWhatsAppMetaWebhook(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp)
{
    std::string verifyToken;
    std::string appSecret;
    bool enabled = false;
    {
        std::lock_guard<std::mutex> lock(configMutex);
        auto it = config->channels.find("whatsapp_meta");

        if (it != config->channels.end() && it->second.enabled && it->second.raw.is_object())
        {
            enabled = true;
            verifyToken = it->second.raw.value("verify_token", "");
            appSecret = it->second.raw.value("app_secret", "");
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
            spdlog::warn("[Routes] whatsapp meta webhook signature verification failed");
            resp.setStatus(Poco::Net::HTTPResponse::HTTP_FORBIDDEN);
            sendJson(resp, {{"error", "invalid signature"}});
            return;
        }

        auto body = nlohmann::json::parse(rawBody);

        for (const auto &msg : ionclaw::channel::WhatsAppWebhook::parseMeta(body))
        {
            if (!webhookDedup.markSeen(msg.msgId))
            {
                continue;
            }

            publishWhatsAppInbound(msg);
        }

        sendJson(resp, {{"status", "ok"}});
    }
    catch (const std::exception &e)
    {
        spdlog::error("[Routes] whatsapp meta webhook error: {}", e.what());
        sendError(resp, e.what(), 500);
    }
}

} // namespace server
} // namespace ionclaw
