#include "doctest/doctest.h"

#include "ionclaw/util/StringHelper.hpp"

using namespace ionclaw::util;

TEST_CASE("utf8SafeTruncate never splits a multi-byte code point")
{
    // "aé" where é is the 2-byte sequence 0xC3 0xA9
    std::string text = "a\xC3\xA9";
    CHECK(StringHelper::utf8SafeTruncate(text, 10) == text);

    // cutting at 2 bytes would split é, so it drops back to just "a"
    auto truncated = StringHelper::utf8SafeTruncate(text, 2);
    CHECK(truncated == "a");
}

TEST_CASE("utf8SafeTruncate keeps plain ascii intact")
{
    CHECK(StringHelper::utf8SafeTruncate("hello", 3) == "hel");
}

TEST_CASE("trim strips surrounding whitespace")
{
    CHECK(StringHelper::trim("  hi \t\n") == "hi");
    CHECK(StringHelper::trim("   ").empty());
    CHECK(StringHelper::trim("nows") == "nows");
}

TEST_CASE("toLower lowercases ascii and leaves multibyte bytes untouched")
{
    CHECK(StringHelper::toLower("HeLLo") == "hello");
    CHECK(StringHelper::toLower("Café") == "café");
}

TEST_CASE("unquote removes matching surrounding quotes")
{
    CHECK(StringHelper::unquote("\"quoted\"") == "quoted");
    CHECK(StringHelper::unquote("'quoted'") == "quoted");
    CHECK(StringHelper::unquote("plain") == "plain");
    CHECK(StringHelper::unquote("\"mismatch'") == "\"mismatch'");
}

TEST_CASE("sanitizeForPrompt drops control and zero-width characters")
{
    // zero-width space U+200B is 0xE2 0x80 0x8B (split to stop the hex escape from eating 'b')
    std::string withZwsp = "a\xE2\x80\x8B" "b";
    CHECK(StringHelper::sanitizeForPrompt(withZwsp) == "ab");

    // NUL and other C0 controls are dropped, tabs and newlines survive
    std::string withCtrl = std::string("x\x01y\tz");
    CHECK(StringHelper::sanitizeForPrompt(withCtrl) == "xy\tz");
}

TEST_CASE("urlEncode escapes reserved characters")
{
    CHECK(StringHelper::urlEncode("a b") == "a+b");
    CHECK(StringHelper::urlEncode("a/b?c") == "a%2Fb%3Fc");
    CHECK(StringHelper::urlEncode("safe-._~") == "safe-._~");
}

TEST_CASE("redactSensitive masks api keys and bearer tokens")
{
    auto redacted = StringHelper::redactSensitive("token sk-abcdefghij1234567890 here");
    CHECK(redacted.find("sk-abcdefghij1234567890") == std::string::npos);

    auto pem = StringHelper::redactSensitive("-----BEGIN PRIVATE KEY-----\nabcd\n-----END PRIVATE KEY-----");
    CHECK(pem == "[REDACTED PRIVATE KEY]");
}

TEST_CASE("stripReasoningTags removes thinking blocks and keeps final content")
{
    CHECK(StringHelper::stripReasoningTags("<think>hidden</think>answer") == "answer");
    CHECK(StringHelper::stripReasoningTags("<final>done</final>") == "done");
    CHECK(StringHelper::stripReasoningTags("plain text") == "plain text");
}
