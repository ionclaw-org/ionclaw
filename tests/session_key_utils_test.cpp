#include "doctest/doctest.h"

#include "ionclaw/session/SessionKeyUtils.hpp"

using namespace ionclaw::session;

TEST_CASE("build composes an agent-scoped key")
{
    CHECK(SessionKeyUtils::build("main", "web", "t1") == "agent:main:web:t1");
    CHECK(SessionKeyUtils::buildFromBase("main", "web:t1") == "agent:main:web:t1");
}

TEST_CASE("extractChannel and extractChatId work on plain base keys")
{
    CHECK(SessionKeyUtils::extractChannel("web:t1") == "web");
    CHECK(SessionKeyUtils::extractChatId("web:t1") == "t1");
}

TEST_CASE("extractChannel and extractChatId work on agent-scoped keys")
{
    auto key = SessionKeyUtils::build("main", "telegram", "12345");
    CHECK(SessionKeyUtils::extractChannel(key) == "telegram");
    CHECK(SessionKeyUtils::extractChatId(key) == "12345");
    CHECK(SessionKeyUtils::extractBaseKey(key) == "telegram:12345");
}

TEST_CASE("chatId keeps trailing colons in the identifier")
{
    CHECK(SessionKeyUtils::extractChatId("web:a:b") == "a:b");
}

TEST_CASE("isAgentScoped distinguishes scoped from plain keys")
{
    CHECK(SessionKeyUtils::isAgentScoped("agent:main:web:t1"));
    CHECK_FALSE(SessionKeyUtils::isAgentScoped("web:t1"));
    CHECK_FALSE(SessionKeyUtils::isAgentScoped("agent:"));
}

TEST_CASE("parse returns nullopt for non-scoped keys and fills fields for scoped keys")
{
    CHECK_FALSE(SessionKeyUtils::parse("web:t1").has_value());

    auto parsed = SessionKeyUtils::parse("agent:helper:web:room-9");
    REQUIRE(parsed.has_value());
    CHECK(parsed->agentId == "helper");
    CHECK(parsed->channel == "web");
    CHECK(parsed->chatId == "room-9");
    CHECK(parsed->baseKey == "web:room-9");
}
