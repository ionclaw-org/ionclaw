#include "doctest/doctest.h"

#include "ionclaw/util/EnvironmentHelper.hpp"

using ionclaw::util::EnvironmentHelper;

TEST_CASE("expandEnvVars substitutes a defined variable and leaves the surrounding text")
{
    EnvironmentHelper::set("IONCLAW_TEST_HOST", "example.com");
    CHECK(EnvironmentHelper::expandEnvVars("https://${IONCLAW_TEST_HOST}/api") == "https://example.com/api");
    EnvironmentHelper::unset("IONCLAW_TEST_HOST");
}

TEST_CASE("an undefined variable expands to empty")
{
    EnvironmentHelper::unset("IONCLAW_TEST_MISSING");
    CHECK(EnvironmentHelper::expandEnvVars("a${IONCLAW_TEST_MISSING}b") == "ab");
}

TEST_CASE("multiple references are all expanded in one pass")
{
    EnvironmentHelper::set("IONCLAW_TEST_A", "1");
    EnvironmentHelper::set("IONCLAW_TEST_B", "2");
    CHECK(EnvironmentHelper::expandEnvVars("${IONCLAW_TEST_A}-${IONCLAW_TEST_B}-${IONCLAW_TEST_A}") == "1-2-1");
    EnvironmentHelper::unset("IONCLAW_TEST_A");
    EnvironmentHelper::unset("IONCLAW_TEST_B");
}

TEST_CASE("a substituted value is never rescanned for further references")
{
    // regression: a single left-to-right pass must not re-expand text that came from a variable's value
    EnvironmentHelper::set("IONCLAW_TEST_OUTER", "${IONCLAW_TEST_INNER}");
    EnvironmentHelper::set("IONCLAW_TEST_INNER", "leaked");

    CHECK(EnvironmentHelper::expandEnvVars("${IONCLAW_TEST_OUTER}") == "${IONCLAW_TEST_INNER}");

    EnvironmentHelper::unset("IONCLAW_TEST_OUTER");
    EnvironmentHelper::unset("IONCLAW_TEST_INNER");
}

TEST_CASE("text without any reference is returned unchanged")
{
    CHECK(EnvironmentHelper::expandEnvVars("plain text with $ and { but no ref") == "plain text with $ and { but no ref");
}

TEST_CASE("isSet reflects set and unset")
{
    EnvironmentHelper::set("IONCLAW_TEST_FLAG", "x");
    CHECK(EnvironmentHelper::isSet("IONCLAW_TEST_FLAG"));
    EnvironmentHelper::unset("IONCLAW_TEST_FLAG");
    CHECK_FALSE(EnvironmentHelper::isSet("IONCLAW_TEST_FLAG"));
}
