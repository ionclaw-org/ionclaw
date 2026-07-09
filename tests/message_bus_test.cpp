#include "doctest/doctest.h"

#include "ionclaw/bus/MessageBus.hpp"

using namespace ionclaw::bus;

namespace
{

InboundMessage makeInbound(const std::string &chatId, const std::string &content)
{
    InboundMessage msg;
    msg.channel = "test";
    msg.senderId = "sender";
    msg.chatId = chatId;
    msg.content = content;
    return msg;
}

} // namespace

TEST_CASE("publishInbound accepts a message and rejects its immediate duplicate")
{
    MessageBus bus;

    CHECK(bus.publishInbound(makeInbound("a", "hello")) == PublishResult::Accepted);
    CHECK(bus.publishInbound(makeInbound("a", "hello")) == PublishResult::Duplicate);

    // a different content on the same chat is not a duplicate
    CHECK(bus.publishInbound(makeInbound("a", "world")) == PublishResult::Accepted);
}

TEST_CASE("a message rejected for a full queue can be delivered after the queue drains")
{
    MessageBus bus;

    // saturate the queue with distinct sessions so neither dedup nor the per-session rate limit interferes
    for (int i = 0; i < 1000; ++i)
    {
        REQUIRE(bus.publishInbound(makeInbound("chat" + std::to_string(i), "m" + std::to_string(i))) == PublishResult::Accepted);
    }

    // the queue is full, so this message is rejected without being recorded as seen
    auto retried = makeInbound("late", "retry-me");
    CHECK(bus.publishInbound(retried) == PublishResult::QueueFull);

    // drain everything the consumer would process
    InboundMessage drained;
    int consumed = 0;

    while (bus.consumeInbound(drained, 0))
    {
        ++consumed;
    }

    CHECK(consumed == 1000);

    // the retry must now be accepted, not silently swallowed as a duplicate of the rejected attempt
    CHECK(bus.publishInbound(retried) == PublishResult::Accepted);
}

TEST_CASE("a synthetic message is never treated as a duplicate")
{
    MessageBus bus;

    auto wake = makeInbound("a", "wake");
    wake.metadata["synthetic"] = true;

    CHECK(bus.publishInbound(wake) == PublishResult::Accepted);
    CHECK(bus.publishInbound(wake) == PublishResult::Accepted);
}
