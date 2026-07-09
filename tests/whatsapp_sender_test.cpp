#include "doctest/doctest.h"

#include "nlohmann/json.hpp"

#include "ionclaw/channel/WhatsAppSender.hpp"

using namespace ionclaw::channel;

TEST_CASE("z-api send-text builds the url, client-token header and body")
{
    auto req = WhatsAppSender::zApiSendText("https://api.z-api.io/instances/ID/token/TK", "CT", "5511", "hi", 3);
    CHECK(req.method == "POST");
    CHECK(req.url == "https://api.z-api.io/instances/ID/token/TK/send-text");
    CHECK(req.headers.at("Client-Token") == "CT");
    CHECK(req.headers.at("Content-Type") == "application/json");

    auto body = nlohmann::json::parse(req.body);
    CHECK(body["phone"] == "5511");
    CHECK(body["message"] == "hi");
    CHECK(body["delayTyping"] == 3);
}

TEST_CASE("z-api send-image and send-document route correctly")
{
    auto img = WhatsAppSender::zApiSendMedia("https://b", "CT", "55", "image", "data:image/png;base64,AAA", "cap", "");
    CHECK(img.url == "https://b/send-image");
    auto ibody = nlohmann::json::parse(img.body);
    CHECK(ibody["image"] == "data:image/png;base64,AAA");
    CHECK(ibody["caption"] == "cap");

    auto doc = WhatsAppSender::zApiSendMedia("https://b", "CT", "55", "document", "data:application/pdf;base64,BBB", "", "report.pdf");
    CHECK(doc.url == "https://b/send-document/pdf");
    auto dbody = nlohmann::json::parse(doc.body);
    CHECK(dbody["fileName"] == "report.pdf");
}

TEST_CASE("meta send-text builds the graph url, bearer header and body")
{
    auto req = WhatsAppSender::metaSendText("v23.0", "PID", "TOKEN", "16505551234", "hello");
    CHECK(req.url == "https://graph.facebook.com/v23.0/PID/messages");
    CHECK(req.headers.at("Authorization") == "Bearer TOKEN");

    auto body = nlohmann::json::parse(req.body);
    CHECK(body["messaging_product"] == "whatsapp");
    CHECK(body["to"] == "16505551234");
    CHECK(body["type"] == "text");
    CHECK(body["text"]["body"] == "hello");
}

TEST_CASE("meta send-media builds an image message by link with a caption")
{
    auto req = WhatsAppSender::metaSendMedia("v23.0", "PID", "TOKEN", "55", "image", "https://host/public/media/a.png", "cap", "a.png");
    CHECK(req.url == "https://graph.facebook.com/v23.0/PID/messages");
    CHECK(req.headers.at("Authorization") == "Bearer TOKEN");

    auto body = nlohmann::json::parse(req.body);
    CHECK(body["type"] == "image");
    CHECK(body["image"]["link"] == "https://host/public/media/a.png");
    CHECK(body["image"]["caption"] == "cap");
}

TEST_CASE("meta send-media carries the filename for documents and omits captions for audio")
{
    auto doc = WhatsAppSender::metaSendMedia("v23.0", "PID", "TOKEN", "55", "document", "https://host/public/r.pdf", "here", "r.pdf");
    auto dbody = nlohmann::json::parse(doc.body);
    CHECK(dbody["type"] == "document");
    CHECK(dbody["document"]["filename"] == "r.pdf");
    CHECK(dbody["document"]["caption"] == "here");

    auto audio = WhatsAppSender::metaSendMedia("v23.0", "PID", "TOKEN", "55", "audio", "https://host/public/v.ogg", "ignored", "v.ogg");
    auto abody = nlohmann::json::parse(audio.body);
    CHECK(abody["type"] == "audio");
    CHECK(abody["audio"]["link"] == "https://host/public/v.ogg");
    CHECK_FALSE(abody["audio"].contains("caption"));
}

TEST_CASE("meta read+typing references the inbound message id")
{
    auto req = WhatsAppSender::metaSendReadAndTyping("v23.0", "PID", "TOKEN", "wamid.XYZ");
    auto body = nlohmann::json::parse(req.body);
    CHECK(body["status"] == "read");
    CHECK(body["message_id"] == "wamid.XYZ");
    CHECK(body["typing_indicator"]["type"] == "text");
}
