#include "doctest/doctest.h"

#include "ionclaw/provider/ProviderHelper.hpp"

using namespace ionclaw::provider;

TEST_CASE("stripProviderPrefix removes the provider segment")
{
    CHECK(ProviderHelper::stripProviderPrefix("anthropic/claude-sonnet-4-6") == "claude-sonnet-4-6");
    CHECK(ProviderHelper::stripProviderPrefix("gpt-4o") == "gpt-4o");
    CHECK(ProviderHelper::stripProviderPrefix("ollama/qwen2.5:0.5b") == "qwen2.5:0.5b");
}

TEST_CASE("sanitizeToolCallId keeps valid ids and rewrites invalid characters")
{
    CHECK(ProviderHelper::sanitizeToolCallId("call_abc-123") == "call_abc-123");
    CHECK(ProviderHelper::sanitizeToolCallId("bad id!@#") == "bad_id___");

    auto generated = ProviderHelper::sanitizeToolCallId("");
    CHECK(generated.rfind("call_", 0) == 0);
    CHECK(generated.size() > 5);
}

TEST_CASE("repairJsonArgs parses valid json and closes unbalanced delimiters")
{
    CHECK(ProviderHelper::repairJsonArgs("").is_object());
    CHECK(ProviderHelper::repairJsonArgs(R"({"a":1})")["a"] == 1);

    auto closed = ProviderHelper::repairJsonArgs(R"({"a":"b")");
    CHECK(closed["a"] == "b");

    auto trailing = ProviderHelper::repairJsonArgs(R"({"a":1,})");
    CHECK(trailing["a"] == 1);

    CHECK(ProviderHelper::repairJsonArgs("not json at all").is_object());
}

TEST_CASE("classifyError maps messages to their categories")
{
    CHECK(ProviderHelper::classifyError("maximum context length exceeded") == "context_overflow");
    CHECK(ProviderHelper::classifyError("Rate limit reached") == "rate_limit");
    CHECK(ProviderHelper::classifyError("HTTP 429 Too Many Requests") == "rate_limit");
    CHECK(ProviderHelper::classifyError("insufficient_quota") == "billing");
    CHECK(ProviderHelper::classifyError("401 Unauthorized") == "auth");
    CHECK(ProviderHelper::classifyError("The model does not exist") == "model_not_found");
    CHECK(ProviderHelper::classifyError("request timed out") == "timeout");
    CHECK(ProviderHelper::classifyError("roles must alternate") == "role_ordering");
    CHECK(ProviderHelper::classifyError("502 bad gateway") == "transient");
    CHECK(ProviderHelper::classifyError("something odd") == "unknown");
}

TEST_CASE("classifyError does not treat digits inside numbers as status codes")
{
    CHECK(ProviderHelper::classifyError("cost was 4290 credits") == "unknown");
}

TEST_CASE("sanitizeErrorMessage redacts secrets")
{
    auto safe = ProviderHelper::sanitizeErrorMessage("bad key sk-abcdef0123456789 used");
    CHECK(safe.find("sk-abcdef0123456789") == std::string::npos);
    CHECK(safe.find("[REDACTED]") != std::string::npos);

    auto bearer = ProviderHelper::sanitizeErrorMessage("Authorization: Bearer abcdef0123456789xyz");
    CHECK(bearer.find("abcdef0123456789xyz") == std::string::npos);
}

TEST_CASE("sanitizeToolCallName enforces the allowed charset and length")
{
    CHECK(ProviderHelper::sanitizeToolCallName("read_file") == "read_file");
    CHECK(ProviderHelper::sanitizeToolCallName("weird name!") == "weirdname");
    CHECK(ProviderHelper::sanitizeToolCallName("!!!") == "unknown_tool");
    CHECK(ProviderHelper::sanitizeToolCallName(std::string(100, 'a')).size() == 64);
}
