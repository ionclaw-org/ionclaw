#include "doctest/doctest.h"

#include <atomic>
#include <stdexcept>
#include <thread>
#include <vector>

#include "ionclaw/config/ConfigStore.hpp"

using namespace ionclaw::config;

TEST_CASE("snapshot returns the current config")
{
    Config initial;
    initial.timezone = "UTC";

    ConfigStore store(initial);
    auto snap = store.snapshot();

    REQUIRE(snap != nullptr);
    CHECK(snap->timezone == "UTC");
}

TEST_CASE("replace swaps in a new config for later snapshots")
{
    Config initial;
    initial.timezone = "UTC";
    ConfigStore store(initial);

    Config next;
    next.timezone = "America/Sao_Paulo";
    store.replace(next);

    CHECK(store.snapshot()->timezone == "America/Sao_Paulo");
}

TEST_CASE("a snapshot taken before a replace keeps its own immutable value")
{
    Config initial;
    initial.timezone = "UTC";
    ConfigStore store(initial);

    // an in-flight reader holds this snapshot across a concurrent replace
    auto held = store.snapshot();

    Config next;
    next.timezone = "America/Sao_Paulo";
    store.replace(next);

    // the held snapshot is unaffected, which is what makes lock-free reads safe against a restart
    CHECK(held->timezone == "UTC");
    CHECK(store.snapshot()->timezone == "America/Sao_Paulo");
}

TEST_CASE("update copies, mutates and commits the config")
{
    Config initial;
    initial.timezone = "UTC";
    ConfigStore store(initial);

    store.update([](Config &config) { config.timezone = "Europe/Lisbon"; });

    CHECK(store.snapshot()->timezone == "Europe/Lisbon");
}

TEST_CASE("an update whose mutator throws commits nothing")
{
    Config initial;
    initial.timezone = "UTC";
    ConfigStore store(initial);

    // clang-format off
    CHECK_THROWS_AS(store.update([](Config &config) {
        config.timezone = "half-applied";
        throw std::runtime_error("validation failed");
    }), std::runtime_error);
    // clang-format on

    // the rejected edit is discarded, so the live config is untouched
    CHECK(store.snapshot()->timezone == "UTC");
}

TEST_CASE("concurrent updates and reads never tear a config")
{
    Config initial;
    initial.timezone = "tz-init";
    ConfigStore store(initial);

    constexpr int writers = 6;
    constexpr int readers = 6;
    constexpr int iterations = 500;

    std::atomic<bool> stop{false};
    std::atomic<int> badReads{0};
    std::vector<std::thread> pool;

    for (int r = 0; r < readers; ++r)
    {
        // clang-format off
        pool.emplace_back([&store, &stop, &badReads] {
            while (!stop.load())
            {
                auto snap = store.snapshot();
                // a snapshot is always a fully committed value, never a partial write
                if (snap->timezone.rfind("tz-", 0) != 0)
                {
                    badReads.fetch_add(1);
                }
            }
        });
        // clang-format on
    }

    for (int w = 0; w < writers; ++w)
    {
        // clang-format off
        pool.emplace_back([&store, w] {
            for (int i = 0; i < iterations; ++i)
            {
                store.update([w, i](Config &config) { config.timezone = "tz-" + std::to_string(w) + "-" + std::to_string(i); });
            }
        });
        // clang-format on
    }

    // writers finish, then readers stop
    for (int w = 0; w < writers; ++w)
    {
        pool[readers + w].join();
    }

    stop.store(true);

    for (int r = 0; r < readers; ++r)
    {
        pool[r].join();
    }

    CHECK(badReads.load() == 0);
    CHECK(store.snapshot()->timezone.rfind("tz-", 0) == 0);
}
