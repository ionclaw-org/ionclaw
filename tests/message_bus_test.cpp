#include "doctest/doctest.h"

#include <atomic>
#include <thread>
#include <vector>

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

OutboundMessage makeOutbound(const std::string &channel, const std::string &content)
{
    OutboundMessage msg;
    msg.channel = channel;
    msg.chatId = "chat";
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

TEST_CASE("the outbound queue is bounded so a stalled channel runner cannot grow it forever")
{
    MessageBus bus;

    // publish far beyond the cap for a channel that never drains
    for (int i = 0; i < 1500; ++i)
    {
        bus.publishOutbound(makeOutbound("dead", "m" + std::to_string(i)));
    }

    int drained = 0;
    OutboundMessage out;

    while (bus.consumeOutbound("dead", out, 0))
    {
        ++drained;
    }

    // the oldest were dropped once the cap was reached, so memory stayed bounded
    CHECK(drained <= 1000);
    CHECK(drained > 0);
}

TEST_CASE("concurrent inbound publishers and a consumer lose no accepted message")
{
    MessageBus bus;

    constexpr int publishers = 6;
    constexpr int perPublisher = 100;
    std::atomic<int> accepted{0};
    std::vector<std::thread> workers;

    for (int p = 0; p < publishers; ++p)
    {
        // clang-format off
        workers.emplace_back([&bus, &accepted, p] {
            for (int i = 0; i < perPublisher; ++i)
            {
                auto msg = makeInbound("chat-" + std::to_string(p) + "-" + std::to_string(i), "body");
                if (bus.publishInbound(msg) == PublishResult::Accepted)
                {
                    accepted.fetch_add(1);
                }
            }
        });
        // clang-format on
    }

    std::atomic<bool> producersDone{false};
    std::atomic<int> consumed{0};

    // clang-format off
    std::thread consumer([&bus, &producersDone, &consumed] {
        InboundMessage msg;
        while (true)
        {
            if (bus.consumeInbound(msg, 5))
            {
                consumed.fetch_add(1);
                continue;
            }

            if (producersDone.load())
            {
                while (bus.consumeInbound(msg, 0))
                {
                    consumed.fetch_add(1);
                }

                break;
            }
        }
    });
    // clang-format on

    for (auto &w : workers)
    {
        w.join();
    }

    producersDone.store(true);
    consumer.join();

    // every message the bus accepted is consumed exactly once, none lost under concurrency
    CHECK(consumed.load() == accepted.load());
}
