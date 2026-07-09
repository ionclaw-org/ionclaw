#include "doctest/doctest.h"

#include "ionclaw/agent/ContextWindow.hpp"
#include "ionclaw/provider/OpenAiProvider.hpp"

namespace ionclaw
{
namespace provider
{

// friended by OpenAiProvider so the tests can exercise the stateless response transform directly
struct OpenAiProviderTestAccess
{
    static ChatCompletionResponse parse(const OpenAiProvider &provider, const nlohmann::json &response)
    {
        return provider.parseResponse(response);
    }

    static nlohmann::json build(const OpenAiProvider &provider, const ChatCompletionRequest &request)
    {
        return provider.buildRequestBody(request);
    }
};

} // namespace provider
} // namespace ionclaw

using namespace ionclaw::provider;

TEST_CASE("parseResponse handles a tool-call message whose content is null")
{
    OpenAiProvider provider("sk-test");

    // openai emits content: null on any tool-call response; value() would throw type_error.302 on this
    nlohmann::json response = {
        {"choices", nlohmann::json::array({
                        {
                            {"message", {{"role", "assistant"}, {"content", nullptr}, {"tool_calls", nlohmann::json::array({{{"id", "call_1"}, {"type", "function"}, {"function", {{"name", "read"}, {"arguments", "{}"}}}}})}}},
                            {"finish_reason", "tool_calls"},
                        },
                    })},
    };

    ChatCompletionResponse parsed;
    REQUIRE_NOTHROW(parsed = OpenAiProviderTestAccess::parse(provider, response));

    CHECK(parsed.content.empty());
    CHECK(parsed.finishReason == "tool_calls");
    REQUIRE(parsed.toolCalls.size() == 1);
    CHECK(parsed.toolCalls[0].name == "read");
}

TEST_CASE("parseResponse tolerates a null finish_reason")
{
    OpenAiProvider provider("sk-test");

    nlohmann::json response = {
        {"choices", nlohmann::json::array({
                        {
                            {"message", {{"role", "assistant"}, {"content", "hi"}}},
                            {"finish_reason", nullptr},
                        },
                    })},
    };

    ChatCompletionResponse parsed;
    REQUIRE_NOTHROW(parsed = OpenAiProviderTestAccess::parse(provider, response));

    CHECK(parsed.content == "hi");
    CHECK(parsed.finishReason == "stop");
}

TEST_CASE("buildRequestBody drops tools for a model that cannot call functions and keeps them otherwise")
{
    OpenAiProvider provider("sk-test");

    ChatCompletionRequest request;
    request.tools = {{{"name", "read"}, {"description", "read a file"}, {"parameters", nlohmann::json::object()}}};

    // a chat model the table marks as non-function-calling must not receive a tools payload
    request.model = "deepseek-reasoner";
    auto dropped = OpenAiProviderTestAccess::build(provider, request);
    CHECK_FALSE(dropped.contains("tools"));

    // a function-calling model keeps its tools
    request.model = "gpt-4o";
    auto kept = OpenAiProviderTestAccess::build(provider, request);
    REQUIRE(kept.contains("tools"));
    CHECK(kept["tools"].size() == 1);
}

TEST_CASE("getModelLimit resolves the hyphenated claude 3.5 and 3.7 model ids")
{
    using ionclaw::agent::ContextWindow;

    CHECK(ContextWindow::getModelLimit("claude-3-5-sonnet-20241022") == 200000);
    CHECK(ContextWindow::getModelLimit("claude-3-7-sonnet-latest") == 200000);
}
