#include "ionclaw/channel/WhatsAppWebhook.hpp"

#include <vector>

#include "Poco/DigestEngine.h"
#include "Poco/SHA2Engine.h"

namespace ionclaw
{
namespace channel
{

// sha-256 over a byte range, returning the raw 32-byte digest
std::vector<unsigned char> WhatsAppWebhook::sha256Bytes(const unsigned char *data, size_t len)
{
    Poco::SHA2Engine engine(Poco::SHA2Engine::SHA_256);
    engine.update(data, len);
    return engine.digest();
}

std::string WhatsAppWebhook::hmacSha256Hex(const std::string &key, const std::string &data)
{
    // sha-256 block size is 64 bytes
    constexpr size_t blockSize = 64;

    std::vector<unsigned char> keyBlock(blockSize, 0);

    if (key.size() > blockSize)
    {
        auto hashed = sha256Bytes(reinterpret_cast<const unsigned char *>(key.data()), key.size());
        std::copy(hashed.begin(), hashed.end(), keyBlock.begin());
    }
    else
    {
        std::copy(key.begin(), key.end(), keyBlock.begin());
    }

    std::vector<unsigned char> inner(blockSize);
    std::vector<unsigned char> outer(blockSize);

    for (size_t i = 0; i < blockSize; ++i)
    {
        inner[i] = keyBlock[i] ^ 0x36;
        outer[i] = keyBlock[i] ^ 0x5c;
    }

    inner.insert(inner.end(), data.begin(), data.end());
    auto innerHash = sha256Bytes(inner.data(), inner.size());

    outer.insert(outer.end(), innerHash.begin(), innerHash.end());
    auto finalHash = sha256Bytes(outer.data(), outer.size());

    return Poco::DigestEngine::digestToHex(finalHash);
}

std::string WhatsAppWebhook::strField(const nlohmann::json &obj, const char *key, const std::string &fallback)
{
    auto it = obj.find(key);
    return it != obj.end() && it->is_string() ? it->get<std::string>() : fallback;
}

bool WhatsAppWebhook::boolField(const nlohmann::json &obj, const char *key, bool fallback)
{
    auto it = obj.find(key);
    return it != obj.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

ParsedWebhookMessage WhatsAppWebhook::parseZApi(const nlohmann::json &body)
{
    ParsedWebhookMessage msg;

    if (!body.is_object() || !body.contains("phone") || !body["phone"].is_string())
    {
        return msg;
    }

    msg.fromMe = boolField(body, "fromMe");
    msg.isGroup = boolField(body, "isGroup");
    msg.senderPhone = strField(body, "phone");
    msg.senderName = strField(body, "senderName");
    msg.msgId = strField(body, "messageId");

    if (body.contains("text") && body["text"].is_object())
    {
        msg.text = strField(body["text"], "message");
    }

    // each media type is its own object and carries its own caption
    // clang-format off
    auto addMedia = [&](const char *key, const std::string &kind, const char *urlField, const char *fileNameField) {
        if (!body.contains(key) || !body[key].is_object())
        {
            return;
        }

        const auto &m = body[key];
        WebhookMedia media;
        media.kind = kind;
        media.url = strField(m, urlField);
        media.mimeType = strField(m, "mimeType");
        media.caption = strField(m, "caption");

        if (fileNameField != nullptr)
        {
            media.fileName = strField(m, fileNameField);
        }

        if (!media.url.empty())
        {
            msg.media.push_back(std::move(media));
        }
    };
    // clang-format on

    addMedia("image", "image", "imageUrl", nullptr);
    addMedia("audio", "audio", "audioUrl", nullptr);
    addMedia("video", "video", "videoUrl", nullptr);
    addMedia("document", "document", "documentUrl", "fileName");

    // require some payload to treat it as a real inbound message
    msg.valid = !msg.text.empty() || !msg.media.empty();
    return msg;
}

std::vector<ParsedWebhookMessage> WhatsAppWebhook::parseMeta(const nlohmann::json &body)
{
    std::vector<ParsedWebhookMessage> result;

    if (!body.is_object() || !body.contains("entry") || !body["entry"].is_array())
    {
        return result;
    }

    for (const auto &entry : body["entry"])
    {
        if (!entry.contains("changes") || !entry["changes"].is_array())
        {
            continue;
        }

        for (const auto &change : entry["changes"])
        {
            if (!change.contains("value") || !change["value"].is_object())
            {
                continue;
            }

            const auto &value = change["value"];

            // the contact name lives once per value, alongside the messages array
            std::string senderName;

            if (value.contains("contacts") && value["contacts"].is_array() && !value["contacts"].empty())
            {
                const auto &contact = value["contacts"][0];

                if (contact.contains("profile") && contact["profile"].is_object())
                {
                    senderName = strField(contact["profile"], "name");
                }
            }

            if (!value.contains("messages") || !value["messages"].is_array())
            {
                continue;
            }

            for (const auto &m : value["messages"])
            {
                ParsedWebhookMessage msg;
                msg.senderPhone = strField(m, "from");
                msg.msgId = strField(m, "id");
                msg.senderName = senderName;

                auto type = strField(m, "type");

                if (type == "text" && m.contains("text") && m["text"].is_object())
                {
                    msg.text = strField(m["text"], "body");
                }
                else if (!type.empty() && m.contains(type) && m[type].is_object())
                {
                    // image/audio/video/document/sticker: the media object is keyed by the type, fetched later by id
                    const auto &media = m[type];
                    WebhookMedia wm;
                    wm.kind = type;
                    wm.mediaId = strField(media, "id");
                    wm.mimeType = strField(media, "mime_type");
                    wm.caption = strField(media, "caption");
                    wm.fileName = strField(media, "filename");

                    if (!wm.mediaId.empty())
                    {
                        msg.media.push_back(std::move(wm));
                    }
                }

                msg.valid = !msg.senderPhone.empty() && !msg.msgId.empty() && (!msg.text.empty() || !msg.media.empty());

                if (msg.valid)
                {
                    result.push_back(std::move(msg));
                }
            }
        }
    }

    return result;
}

bool WhatsAppWebhook::verifyMetaSignature(const std::string &rawBody, const std::string &appSecret, const std::string &signatureHeader)
{
    if (appSecret.empty() || signatureHeader.empty())
    {
        return false;
    }

    // the header is "sha256=<hex>"
    const std::string prefix = "sha256=";

    if (signatureHeader.rfind(prefix, 0) != 0)
    {
        return false;
    }

    auto provided = signatureHeader.substr(prefix.size());
    auto expected = hmacSha256Hex(appSecret, rawBody);

    if (provided.size() != expected.size())
    {
        return false;
    }

    // constant-time comparison so a mismatch does not leak timing
    unsigned char diff = 0;

    for (size_t i = 0; i < expected.size(); ++i)
    {
        diff |= static_cast<unsigned char>(provided[i]) ^ static_cast<unsigned char>(expected[i]);
    }

    return diff == 0;
}

} // namespace channel
} // namespace ionclaw
