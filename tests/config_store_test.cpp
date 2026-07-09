#include "doctest/doctest.h"

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
