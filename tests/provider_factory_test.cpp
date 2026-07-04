#include "doctest/doctest.h"

#include "ionclaw/config/Config.hpp"
#include "ionclaw/provider/ProviderFactory.hpp"

using namespace ionclaw::config;
using namespace ionclaw::provider;

namespace
{

Config makeConfig()
{
    Config cfg;

    CredentialConfig anthropicCred;
    anthropicCred.key = "sk-test-anthropic";
    cfg.credentials["anthropic"] = anthropicCred;

    ProviderConfig anthropic;
    anthropic.name = "anthropic";
    anthropic.credential = "anthropic";
    cfg.providers["anthropic"] = anthropic;

    ProviderConfig ollama;
    ollama.name = "ollama";
    ollama.baseUrl = "http://localhost:11434/v1";
    cfg.providers["ollama"] = ollama;

    return cfg;
}

} // namespace

TEST_CASE("create routes anthropic to the native provider")
{
    auto provider = ProviderFactory::create("anthropic", "sk-test");
    CHECK(provider->name() == "anthropic");
}

TEST_CASE("create routes openai-compatible providers to the openai provider")
{
    for (const auto &name : {"openai", "openrouter", "deepseek", "grok", "gemini", "kimi", "moonshot", "ollama"})
    {
        auto provider = ProviderFactory::create(name, "key");
        CHECK(provider->name() == "openai");
    }
}

TEST_CASE("create falls back to openai-compatible for an unknown provider with a base url")
{
    auto provider = ProviderFactory::create("custom", "key", "https://example.com/v1");
    CHECK(provider->name() == "openai");
}

TEST_CASE("create rejects an unknown provider without a base url")
{
    CHECK_THROWS_AS(ProviderFactory::create("mystery", "key"), std::runtime_error);
}

TEST_CASE("createFromModel resolves the claude-cli prefix to the cli provider")
{
    auto cfg = makeConfig();
    auto provider = ProviderFactory::createFromModel("claude-cli/opus", cfg);
    CHECK(provider->name() == "claude-cli");
}

TEST_CASE("createFromModel rejects a claude-cli model without a name after the prefix")
{
    auto cfg = makeConfig();
    CHECK_THROWS_AS(ProviderFactory::createFromModel("claude-cli/", cfg), std::runtime_error);
}

TEST_CASE("createFromModel resolves an ollama model to the openai-compatible provider")
{
    auto cfg = makeConfig();
    auto provider = ProviderFactory::createFromModel("ollama/qwen2.5-coder:7b", cfg);
    CHECK(provider->name() == "openai");
}

TEST_CASE("createFromModel resolves a native anthropic model")
{
    auto cfg = makeConfig();
    auto provider = ProviderFactory::createFromModel("anthropic/claude-sonnet-4-6", cfg);
    CHECK(provider->name() == "anthropic");
}

TEST_CASE("createFromModel throws for a model whose provider is not configured")
{
    auto cfg = makeConfig();
    CHECK_THROWS_AS(ProviderFactory::createFromModel("unconfigured/model", cfg), std::runtime_error);
}
