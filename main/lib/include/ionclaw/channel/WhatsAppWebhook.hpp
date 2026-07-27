#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace ionclaw
{
namespace channel
{

// a media attachment carried by an inbound webhook message
struct WebhookMedia
{
    std::string kind;     // image | audio | video | document
    std::string url;      // direct download url for z-api, empty for meta
    std::string mediaId;  // media id fetched in two steps for meta, empty for z-api
    std::string mimeType;
    std::string caption;
    std::string fileName;  // documents only
    std::string localPath; // workspace-relative path after the media is downloaded
};

// a normalized inbound message parsed from a whatsapp webhook, independent of provider
struct ParsedWebhookMessage
{
    bool valid = false;
    std::string senderPhone; // e.164 digits, becomes chatId/reply_to
    std::string senderName;
    std::string text;
    std::string msgId; // provider message id, the idempotency key
    std::vector<WebhookMedia> media;
    bool fromMe = false;
    bool isGroup = false;
};

// parsers for the two whatsapp providers plus meta signature verification
class WhatsAppWebhook
{
public:
    // z-api posts a single message object per webhook call
    static ParsedWebhookMessage parseZApi(const nlohmann::json &body);

    // meta wraps messages under entry[].changes[].value.messages[] and one call may carry several
    static std::vector<ParsedWebhookMessage> parseMeta(const nlohmann::json &body);

    // validates the meta X-Hub-Signature-256 header (sha256=<hex>) against hmac-sha256(rawBody, appSecret)
    static bool verifyMetaSignature(const std::string &rawBody, const std::string &appSecret, const std::string &signatureHeader);

private:
    static std::vector<unsigned char> sha256Bytes(const unsigned char *data, size_t len);
    static std::string hmacSha256Hex(const std::string &key, const std::string &data);

    // defensive readers: a provider may send an optional field as null or a wrong type, which json::value() cannot survive
    static std::string strField(const nlohmann::json &obj, const char *key, const std::string &fallback = "");
    static bool boolField(const nlohmann::json &obj, const char *key, bool fallback = false);
};

} // namespace channel
} // namespace ionclaw
