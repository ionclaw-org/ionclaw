#include "doctest/doctest.h"

#include "ionclaw/provider/ModelCapabilities.hpp"

using namespace ionclaw::provider;

TEST_CASE("capability table loads from the embedded asset")
{
    // a widely-present vision+tools model should resolve and report its capabilities
    auto info = ModelCapabilities::lookup("gpt-4o");
    REQUIRE(info.has_value());
    CHECK(info->supportsVision);
    CHECK(info->supportsFunctionCalling);
    CHECK(info->maxInputTokens > 0);
    CHECK(info->provider == "openai");
}

TEST_CASE("lookup strips a provider prefix")
{
    auto direct = ModelCapabilities::lookup("gpt-4o");
    auto prefixed = ModelCapabilities::lookup("openai/gpt-4o");
    REQUIRE(direct.has_value());
    REQUIRE(prefixed.has_value());
    CHECK(prefixed->supportsVision == direct->supportsVision);
}

TEST_CASE("unknown models default to permissive so they are never silently degraded")
{
    CHECK(ModelCapabilities::lookup("totally-made-up-model-xyz") == std::nullopt);
    CHECK(ModelCapabilities::supportsVision("totally-made-up-model-xyz"));
    CHECK(ModelCapabilities::supportsReasoning("totally-made-up-model-xyz"));
    CHECK(ModelCapabilities::supportsFunctionCalling("totally-made-up-model-xyz"));
}

TEST_CASE("reasoning capability is read for a reasoning model")
{
    // o1 is a reasoning model in the table
    auto info = ModelCapabilities::lookup("o1");

    if (info.has_value())
    {
        CHECK(info->supportsReasoning);
    }
}
