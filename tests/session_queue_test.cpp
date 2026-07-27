#include "doctest/doctest.h"

#include <atomic>
#include <thread>
#include <vector>

#include "ionclaw/bus/SessionQueue.hpp"

using namespace ionclaw::bus;

namespace
{

InboundMessage makeMsg(const std::string &chatId, const std::string &content)
{
    InboundMessage msg;
    msg.channel = "test";
    msg.senderId = "sender";
    msg.chatId = chatId;
    msg.content = content;
    return msg;
}

QueueSettings collectSettings(int cap)
{
    QueueSettings s;
    s.mode = QueueMode::Collect;
    s.cap = cap;
    s.debounceMs = 0;
    return s;
}

} // namespace

TEST_CASE("collect mode accumulates messages and depth reflects the count")
{
    SessionQueue queue;
    auto settings = collectSettings(100);

    for (int i = 0; i < 5; ++i)
    {
        queue.enqueue("s", makeMsg("s", "m" + std::to_string(i)), QueueMode::Collect, settings);
    }

    CHECK(queue.depth("s") == 5);
    CHECK(queue.hasPending("s"));

    auto drained = queue.drainFollowup("s");
    CHECK(drained.size() == 5);
    CHECK(queue.depth("s") == 0);
}

TEST_CASE("clear empties a session queue")
{
    SessionQueue queue;
    auto settings = collectSettings(100);

    queue.enqueue("s", makeMsg("s", "a"), QueueMode::Collect, settings);
    queue.enqueue("s", makeMsg("s", "b"), QueueMode::Collect, settings);

    CHECK(queue.clear("s") == 2);
    CHECK(queue.depth("s") == 0);
    CHECK_FALSE(queue.hasPending("s"));
}

TEST_CASE("the hard ceiling rejects unbounded growth on a single session")
{
    SessionQueue queue;
    auto settings = collectSettings(100000); // keep the soft cap out of the way

    int accepted = 0;

    for (int i = 0; i < 600; ++i)
    {
        if (queue.enqueue("flood", makeMsg("flood", "m" + std::to_string(i)), QueueMode::Collect, settings))
        {
            ++accepted;
        }
    }

    // some enqueues are refused once the absolute ceiling is hit, so memory stays bounded
    CHECK(accepted < 600);
    CHECK(queue.depth("flood") <= 500);
}

TEST_CASE("concurrent producers on distinct sessions stay consistent")
{
    SessionQueue queue;
    auto settings = collectSettings(100000);

    constexpr int threads = 8;
    constexpr int perThread = 50;
    std::vector<std::thread> workers;

    for (int t = 0; t < threads; ++t)
    {
        // clang-format off
        workers.emplace_back([&queue, &settings, t] {
            auto key = "sess-" + std::to_string(t);
            for (int i = 0; i < perThread; ++i)
            {
                queue.enqueue(key, makeMsg(key, "m" + std::to_string(i)), QueueMode::Collect, settings);
            }
        });
        // clang-format on
    }

    for (auto &w : workers)
    {
        w.join();
    }

    size_t total = 0;
    for (int t = 0; t < threads; ++t)
    {
        total += queue.depth("sess-" + std::to_string(t));
    }

    CHECK(total == static_cast<size_t>(threads * perThread));
}
