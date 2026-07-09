#include "doctest/doctest.h"

#include "ionclaw/agent/AgentLoop.hpp"

using ionclaw::agent::UsageTracker;

TEST_CASE("record tolerates a null token field and still counts the valid ones")
{
    UsageTracker tracker;

    // openai-compatible proxies emit cache fields as null when caching is unused; value() would throw type_error.302
    nlohmann::json usage = {
        {"prompt_tokens", 100},
        {"completion_tokens", 20},
        {"total_tokens", 120},
        {"cache_creation_input_tokens", nullptr},
        {"cache_read_input_tokens", nullptr},
    };

    REQUIRE_NOTHROW(tracker.record(usage));

    CHECK(tracker.promptTokens == 100);
    CHECK(tracker.lastCallCacheWriteTokens == 0);
}

TEST_CASE("record ignores a non-integer token field")
{
    UsageTracker tracker;

    nlohmann::json usage = {
        {"prompt_tokens", "not-a-number"},
        {"completion_tokens", 5},
    };

    REQUIRE_NOTHROW(tracker.record(usage));

    CHECK(tracker.promptTokens == 0);
    CHECK(tracker.completionTokens == 5);
}

TEST_CASE("record accumulates across calls")
{
    UsageTracker tracker;

    tracker.record({{"prompt_tokens", 10}, {"completion_tokens", 3}});
    tracker.record({{"prompt_tokens", 7}, {"completion_tokens", 2}});

    CHECK(tracker.promptTokens == 17);
    CHECK(tracker.completionTokens == 5);
}
