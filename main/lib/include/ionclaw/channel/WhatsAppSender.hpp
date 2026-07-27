#pragma once

#include <map>
#include <string>

namespace ionclaw
{
namespace channel
{

// a fully-built outbound http request, ready for the http client to execute
struct OutboundRequest
{
    std::string method = "POST";
    std::string url;
    std::map<std::string, std::string> headers;
    std::string body;
};

// builds the outbound send requests for the two whatsapp providers (pure; execution happens elsewhere)
class WhatsAppSender
{
public:
    // z-api base url is https://api.z-api.io/instances/{id}/token/{token}; client token goes in a header on every call
    static OutboundRequest zApiSendText(const std::string &baseUrl, const std::string &clientToken, const std::string &phone, const std::string &text, int delayTyping = 3);

    // z-api media fields accept either a base64 data url or a public url; kind is image|audio|video|document
    static OutboundRequest zApiSendMedia(const std::string &baseUrl, const std::string &clientToken, const std::string &phone, const std::string &kind, const std::string &dataUrl, const std::string &caption, const std::string &fileName);

    // meta graph endpoint is https://graph.facebook.com/{version}/{phoneNumberId}/messages with a bearer token
    static OutboundRequest metaSendText(const std::string &graphVersion, const std::string &phoneNumberId, const std::string &accessToken, const std::string &to, const std::string &text);

    // meta sends media by a public link; kind is image|audio|video|document, caption applies to everything except audio
    static OutboundRequest metaSendMedia(const std::string &graphVersion, const std::string &phoneNumberId, const std::string &accessToken, const std::string &to, const std::string &kind, const std::string &link, const std::string &caption, const std::string &fileName);

    // meta marks the last inbound message read and shows a typing indicator in one call
    static OutboundRequest metaSendReadAndTyping(const std::string &graphVersion, const std::string &phoneNumberId, const std::string &accessToken, const std::string &inboundMessageId);
};

} // namespace channel
} // namespace ionclaw
