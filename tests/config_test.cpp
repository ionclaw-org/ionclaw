#include "doctest/doctest.h"

#include "ionclaw/config/Config.hpp"
#include "ionclaw/config/ConfigLoader.hpp"

using namespace ionclaw::config;

namespace
{

Config makeConfig()
{
    Config cfg;

    ProviderConfig openai;
    openai.name = "openai";
    openai.credential = "openai_cred";
    cfg.providers["openai"] = openai;

    // an entry keyed differently from its declared name, to exercise the name-match fallback
    ProviderConfig kimi;
    kimi.name = "moonshot";
    cfg.providers["kimi"] = kimi;

    CredentialConfig cred;
    cred.key = "sk-openai";
    cfg.credentials["openai_cred"] = cred;

    return cfg;
}

} // namespace

TEST_CASE("findProvider matches a provider by its map key prefix")
{
    auto cfg = makeConfig();
    const auto *provider = cfg.findProvider("openai/gpt-4o");
    REQUIRE(provider != nullptr);
    CHECK(provider->name == "openai");
}

TEST_CASE("findProvider matches a provider by its declared name")
{
    auto cfg = makeConfig();
    const auto *provider = cfg.findProvider("moonshot/kimi-k2");
    REQUIRE(provider != nullptr);
    CHECK(provider->name == "moonshot");
}

TEST_CASE("findProvider returns null for an unknown provider")
{
    auto cfg = makeConfig();
    CHECK(cfg.findProvider("claude-cli/opus") == nullptr);
}

TEST_CASE("findProvider handles a model without a slash")
{
    auto cfg = makeConfig();
    const auto *provider = cfg.findProvider("openai");
    REQUIRE(provider != nullptr);
    CHECK(provider->name == "openai");
}

TEST_CASE("resolveProvider throws when no provider matches")
{
    auto cfg = makeConfig();
    CHECK_THROWS_AS(cfg.resolveProvider("claude-cli/opus"), std::runtime_error);
}

TEST_CASE("resolveProvider returns the matching provider")
{
    auto cfg = makeConfig();
    auto provider = cfg.resolveProvider("openai/gpt-4o");
    CHECK(provider.name == "openai");
}

TEST_CASE("resolveApiKey prefers key over token and returns empty when unset")
{
    auto cfg = makeConfig();
    CHECK(cfg.resolveApiKey("openai") == "sk-openai");
    CHECK(cfg.resolveApiKey("ollama").empty());
    CHECK(cfg.resolveApiKey("nonexistent").empty());
}

TEST_CASE("whatsapp channel credentials survive a config save/load round-trip")
{
    Config cfg;

    ChannelConfig zapi;
    zapi.enabled = true;
    zapi.raw = {{"instance_id", "ID"}, {"instance_token", "IT"}, {"client_token", "CT"}};
    cfg.channels["whatsapp_zapi"] = zapi;

    ChannelConfig meta;
    meta.enabled = true;
    meta.raw = {{"access_token", "AT"}, {"phone_number_id", "PID"}, {"verify_token", "VT"}, {"app_secret", "AS"}, {"graph_version", "v23.0"}};
    cfg.channels["whatsapp_meta"] = meta;

    auto reloaded = ConfigLoader::loadFromString(ConfigLoader::toYaml(cfg));

    auto z = reloaded.channels.find("whatsapp_zapi");
    REQUIRE(z != reloaded.channels.end());
    CHECK(z->second.enabled);
    CHECK(z->second.raw.value("instance_id", "") == "ID");
    CHECK(z->second.raw.value("instance_token", "") == "IT");
    CHECK(z->second.raw.value("client_token", "") == "CT");

    auto m = reloaded.channels.find("whatsapp_meta");
    REQUIRE(m != reloaded.channels.end());
    CHECK(m->second.raw.value("access_token", "") == "AT");
    CHECK(m->second.raw.value("phone_number_id", "") == "PID");
    CHECK(m->second.raw.value("app_secret", "") == "AS");
    CHECK(m->second.raw.value("graph_version", "") == "v23.0");
}
