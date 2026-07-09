#include "doctest/doctest.h"

#include "ionclaw/channel/WhatsAppWebhook.hpp"

using namespace ionclaw::channel;

TEST_CASE("z-api text message parses")
{
    auto body = nlohmann::json::parse(R"({
        "phone":"5511999999999","fromMe":false,"isGroup":false,
        "senderName":"John","messageId":"3EB0","moment":1720000000000,
        "text":{"message":"hello"}
    })");

    auto m = WhatsAppWebhook::parseZApi(body);
    REQUIRE(m.valid);
    CHECK(m.senderPhone == "5511999999999");
    CHECK(m.senderName == "John");
    CHECK(m.msgId == "3EB0");
    CHECK(m.text == "hello");
    CHECK(m.media.empty());
}

TEST_CASE("z-api image message captures the media url and caption")
{
    auto body = nlohmann::json::parse(R"({
        "phone":"5511999999999","messageId":"IMG1","senderName":"John",
        "image":{"imageUrl":"https://cdn/x.jpg","mimeType":"image/jpeg","caption":"look"}
    })");

    auto m = WhatsAppWebhook::parseZApi(body);
    REQUIRE(m.valid);
    REQUIRE(m.media.size() == 1);
    CHECK(m.media[0].kind == "image");
    CHECK(m.media[0].url == "https://cdn/x.jpg");
    CHECK(m.media[0].mimeType == "image/jpeg");
    CHECK(m.media[0].caption == "look");
}

TEST_CASE("z-api fromMe is flagged and empty payload is invalid")
{
    auto body = nlohmann::json::parse(R"({"phone":"55","fromMe":true})");
    auto m = WhatsAppWebhook::parseZApi(body);
    CHECK(m.fromMe);
    CHECK_FALSE(m.valid);
}

TEST_CASE("meta text message parses from the nested envelope")
{
    auto body = nlohmann::json::parse(R"({
        "object":"whatsapp_business_account",
        "entry":[{"changes":[{"field":"messages","value":{
            "contacts":[{"profile":{"name":"Sheena"},"wa_id":"16505551234"}],
            "messages":[{"from":"16505551234","id":"wamid.ABC","type":"text","text":{"body":"hi"}}]
        }}]}]
    })");

    auto msgs = WhatsAppWebhook::parseMeta(body);
    REQUIRE(msgs.size() == 1);
    CHECK(msgs[0].senderPhone == "16505551234");
    CHECK(msgs[0].msgId == "wamid.ABC");
    CHECK(msgs[0].senderName == "Sheena");
    CHECK(msgs[0].text == "hi");
}

TEST_CASE("meta image message carries the media id for later fetch")
{
    auto body = nlohmann::json::parse(R"({
        "entry":[{"changes":[{"value":{
            "messages":[{"from":"165","id":"wamid.IMG","type":"image",
              "image":{"id":"1079","mime_type":"image/jpeg","caption":"c"}}]
        }}]}]
    })");

    auto msgs = WhatsAppWebhook::parseMeta(body);
    REQUIRE(msgs.size() == 1);
    REQUIRE(msgs[0].media.size() == 1);
    CHECK(msgs[0].media[0].mediaId == "1079");
    CHECK(msgs[0].media[0].mimeType == "image/jpeg");
    CHECK(msgs[0].media[0].caption == "c");
}

TEST_CASE("meta status-only payloads yield no messages")
{
    auto body = nlohmann::json::parse(R"({"entry":[{"changes":[{"value":{"statuses":[{"id":"x"}]}}]}]})");
    CHECK(WhatsAppWebhook::parseMeta(body).empty());
}

TEST_CASE("meta signature verification matches a canonical hmac-sha256 vector")
{
    // canonical vector: HMAC-SHA256("key", "The quick brown fox jumps over the lazy dog")
    std::string secret = "key";
    std::string body = "The quick brown fox jumps over the lazy dog";
    std::string good = "sha256=f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8";

    CHECK(WhatsAppWebhook::verifyMetaSignature(body, secret, good));
    CHECK_FALSE(WhatsAppWebhook::verifyMetaSignature(body, secret, "sha256=deadbeef"));
    CHECK_FALSE(WhatsAppWebhook::verifyMetaSignature(body, "wrong-secret", good));
    CHECK_FALSE(WhatsAppWebhook::verifyMetaSignature(body, secret, good.substr(7))); // missing sha256= prefix
    CHECK_FALSE(WhatsAppWebhook::verifyMetaSignature(body, "", good));
}
