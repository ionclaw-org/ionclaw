#include "doctest/doctest.h"

#include "ionclaw/provider/AnthropicProvider.hpp"

namespace ionclaw
{
namespace provider
{

// friended by AnthropicProvider so the tests can exercise the stateless request/response transforms directly
struct AnthropicProviderTestAccess
{
    static nlohmann::json build(const AnthropicProvider &provider, const ChatCompletionRequest &request)
    {
        return provider.buildRequestBody(request);
    }

    static ChatCompletionResponse parse(const AnthropicProvider &provider, const nlohmann::json &response)
    {
        return provider.parseResponse(response);
    }
};

} // namespace provider
} // namespace ionclaw

using namespace ionclaw::provider;

TEST_CASE("parseResponse captures thinking blocks with their signature")
{
    AnthropicProvider provider("sk-test");

    nlohmann::json response = {
        {"content", nlohmann::json::array({
                        {{"type", "thinking"}, {"thinking", "let me reason"}, {"signature", "SIG123"}},
                        {{"type", "text"}, {"text", "hello"}},
                        {{"type", "tool_use"}, {"id", "toolu_1"}, {"name", "read"}, {"input", {{"path", "a.txt"}}}},
                    })},
        {"stop_reason", "tool_use"},
    };

    auto parsed = AnthropicProviderTestAccess::parse(provider, response);

    CHECK(parsed.reasoningContent == "let me reason");
    REQUIRE(parsed.reasoningBlocks.is_array());
    REQUIRE(parsed.reasoningBlocks.size() == 1);
    CHECK(parsed.reasoningBlocks[0].value("type", "") == "thinking");
    CHECK(parsed.reasoningBlocks[0].value("signature", "") == "SIG123");
    REQUIRE(parsed.toolCalls.size() == 1);
}

TEST_CASE("parseResponse preserves redacted thinking blocks verbatim")
{
    AnthropicProvider provider("sk-test");

    nlohmann::json response = {
        {"content", nlohmann::json::array({
                        {{"type", "redacted_thinking"}, {"data", "ENCRYPTED"}},
                        {{"type", "tool_use"}, {"id", "toolu_2"}, {"name", "list"}, {"input", nlohmann::json::object()}},
                    })},
        {"stop_reason", "tool_use"},
    };

    auto parsed = AnthropicProviderTestAccess::parse(provider, response);

    REQUIRE(parsed.reasoningBlocks.is_array());
    REQUIRE(parsed.reasoningBlocks.size() == 1);
    CHECK(parsed.reasoningBlocks[0].value("type", "") == "redacted_thinking");
    CHECK(parsed.reasoningBlocks[0].value("data", "") == "ENCRYPTED");
}

TEST_CASE("buildRequestBody replays thinking blocks before the tool_use blocks")
{
    AnthropicProvider provider("sk-test");

    ChatCompletionRequest request;
    request.model = "claude-sonnet-4";

    Message assistant;
    assistant.role = "assistant";
    assistant.content = "checking that file";
    assistant.reasoningBlocks = nlohmann::json::array({
        {{"type", "thinking"}, {"thinking", "let me reason"}, {"signature", "SIG123"}},
    });

    ToolCall tc;
    tc.id = "toolu_1";
    tc.name = "read";
    tc.arguments = {{"path", "a.txt"}};
    assistant.toolCalls.push_back(tc);

    request.messages.push_back(assistant);

    auto body = AnthropicProviderTestAccess::build(provider, request);

    REQUIRE(body.contains("messages"));
    REQUIRE(body["messages"].is_array());
    REQUIRE(body["messages"].size() == 1);

    const auto &content = body["messages"][0]["content"];
    REQUIRE(content.is_array());

    // thinking must be the first block, its signature intact, then the text and the tool_use
    CHECK(content[0].value("type", "") == "thinking");
    CHECK(content[0].value("signature", "") == "SIG123");
    CHECK(content[1].value("type", "") == "text");
    CHECK(content[2].value("type", "") == "tool_use");
}

TEST_CASE("buildRequestBody adds a system cache_control breakpoint only for caching-capable models")
{
    AnthropicProvider provider("sk-test");

    ChatCompletionRequest request;
    Message system;
    system.role = "system";
    system.content = "you are helpful";
    request.messages.push_back(system);
    Message user;
    user.role = "user";
    user.content = "hi";
    request.messages.push_back(user);

    // a model the table marks as prompt-caching-capable gets the ephemeral breakpoint
    request.model = "claude-sonnet-4-20250514";
    auto cached = AnthropicProviderTestAccess::build(provider, request);
    REQUIRE(cached.contains("system"));
    REQUIRE(cached["system"].is_array());
    CHECK(cached["system"][0].contains("cache_control"));

    // an unknown model is treated conservatively and receives no cache_control
    request.model = "some-unlisted-model";
    auto uncached = AnthropicProviderTestAccess::build(provider, request);
    REQUIRE(uncached.contains("system"));
    CHECK_FALSE(uncached["system"][0].contains("cache_control"));
}

TEST_CASE("buildRequestBody omits reasoning blocks when the assistant turn has none")
{
    AnthropicProvider provider("sk-test");

    ChatCompletionRequest request;
    request.model = "claude-sonnet-4";

    Message assistant;
    assistant.role = "assistant";
    assistant.content = "just a tool call";

    ToolCall tc;
    tc.id = "toolu_9";
    tc.name = "list";
    tc.arguments = nlohmann::json::object();
    assistant.toolCalls.push_back(tc);

    request.messages.push_back(assistant);

    auto body = AnthropicProviderTestAccess::build(provider, request);

    const auto &content = body["messages"][0]["content"];
    REQUIRE(content.is_array());
    CHECK(content[0].value("type", "") == "text");
    CHECK(content[1].value("type", "") == "tool_use");
}
