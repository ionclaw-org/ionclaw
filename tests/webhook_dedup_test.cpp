#include "doctest/doctest.h"

#include "ionclaw/channel/WebhookDedup.hpp"

using namespace ionclaw::channel;

TEST_CASE("a new id is accepted once and rejected on redelivery")
{
    WebhookDedup dedup;
    CHECK(dedup.markSeen("wamid.1"));
    CHECK_FALSE(dedup.markSeen("wamid.1"));
    CHECK_FALSE(dedup.markSeen("wamid.1"));
    CHECK(dedup.markSeen("wamid.2"));
    CHECK(dedup.size() == 2);
}

TEST_CASE("an empty id is always treated as new so nothing is silently dropped")
{
    WebhookDedup dedup;
    CHECK(dedup.markSeen(""));
    CHECK(dedup.markSeen(""));
    CHECK(dedup.size() == 0);
}
