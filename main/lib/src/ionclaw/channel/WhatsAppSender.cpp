#include "ionclaw/channel/WhatsAppSender.hpp"

#include "nlohmann/json.hpp"

namespace ionclaw
{
namespace channel
{

OutboundRequest WhatsAppSender::zApiSendText(const std::string &baseUrl, const std::string &clientToken, const std::string &phone, const std::string &text, int delayTyping)
{
    OutboundRequest req;
    req.url = baseUrl + "/send-text";
    req.headers["Content-Type"] = "application/json";
    req.headers["Client-Token"] = clientToken;
    req.body = nlohmann::json{{"phone", phone}, {"message", text}, {"delayTyping", delayTyping}}.dump();
    return req;
}

OutboundRequest WhatsAppSender::zApiSendMedia(const std::string &baseUrl, const std::string &clientToken, const std::string &phone, const std::string &kind, const std::string &dataUrl, const std::string &caption, const std::string &fileName)
{
    OutboundRequest req;
    req.headers["Content-Type"] = "application/json";
    req.headers["Client-Token"] = clientToken;

    nlohmann::json body;
    body["phone"] = phone;

    if (kind == "image")
    {
        req.url = baseUrl + "/send-image";
        body["image"] = dataUrl;
        body["caption"] = caption;
    }
    else if (kind == "audio")
    {
        req.url = baseUrl + "/send-audio";
        body["audio"] = dataUrl;
    }
    else if (kind == "video")
    {
        req.url = baseUrl + "/send-video";
        body["video"] = dataUrl;
        body["caption"] = caption;
    }
    else
    {
        // documents route by extension and carry the original file name
        req.url = baseUrl + "/send-document/" + fileName.substr(fileName.find_last_of('.') + 1);
        body["document"] = dataUrl;
        body["fileName"] = fileName;
        body["caption"] = caption;
    }

    req.body = body.dump();
    return req;
}

OutboundRequest WhatsAppSender::metaSendText(const std::string &graphVersion, const std::string &phoneNumberId, const std::string &accessToken, const std::string &to, const std::string &text)
{
    OutboundRequest req;
    req.url = "https://graph.facebook.com/" + graphVersion + "/" + phoneNumberId + "/messages";
    req.headers["Content-Type"] = "application/json";
    req.headers["Authorization"] = "Bearer " + accessToken;
    req.body = nlohmann::json{
        {"messaging_product", "whatsapp"},
        {"to", to},
        {"type", "text"},
        {"text", {{"body", text}, {"preview_url", false}}},
    }.dump();
    return req;
}

OutboundRequest WhatsAppSender::metaSendMedia(const std::string &graphVersion, const std::string &phoneNumberId, const std::string &accessToken, const std::string &to, const std::string &kind, const std::string &link, const std::string &caption, const std::string &fileName)
{
    OutboundRequest req;
    req.url = "https://graph.facebook.com/" + graphVersion + "/" + phoneNumberId + "/messages";
    req.headers["Content-Type"] = "application/json";
    req.headers["Authorization"] = "Bearer " + accessToken;

    nlohmann::json object;
    object["link"] = link;

    // audio carries no caption, documents also take the original file name
    if (kind == "image" || kind == "video")
    {
        object["caption"] = caption;
    }
    else if (kind == "document")
    {
        object["caption"] = caption;
        object["filename"] = fileName;
    }

    req.body = nlohmann::json{
        {"messaging_product", "whatsapp"},
        {"to", to},
        {"type", kind},
        {kind, object},
    }.dump();
    return req;
}

OutboundRequest WhatsAppSender::metaSendReadAndTyping(const std::string &graphVersion, const std::string &phoneNumberId, const std::string &accessToken, const std::string &inboundMessageId)
{
    OutboundRequest req;
    req.url = "https://graph.facebook.com/" + graphVersion + "/" + phoneNumberId + "/messages";
    req.headers["Content-Type"] = "application/json";
    req.headers["Authorization"] = "Bearer " + accessToken;
    req.body = nlohmann::json{
        {"messaging_product", "whatsapp"},
        {"status", "read"},
        {"message_id", inboundMessageId},
        {"typing_indicator", {{"type", "text"}}},
    }.dump();
    return req;
}

} // namespace channel
} // namespace ionclaw
