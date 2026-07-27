#include "doctest/doctest.h"

#include "ionclaw/config/Config.hpp"
#include "ionclaw/tool/ToolRegistry.hpp"

using namespace ionclaw::config;
using ionclaw::tool::ToolRegistry;

namespace
{

const std::vector<std::string> ALL_TOOLS = {"read_file", "write_file", "exec", "web_search"};

} // namespace

TEST_CASE("an empty policy allows every tool")
{
    ToolPolicy policy;
    auto result = ToolRegistry::applyToolPolicy(ALL_TOOLS, policy);
    CHECK(result == ALL_TOOLS);
}

TEST_CASE("deny removes the listed tools")
{
    ToolPolicy policy;
    policy.deny = {"exec", "write_file"};
    auto result = ToolRegistry::applyToolPolicy(ALL_TOOLS, policy);
    CHECK(result == std::vector<std::string>{"read_file", "web_search"});
}

TEST_CASE("a non-empty allow list keeps only the listed tools")
{
    ToolPolicy policy;
    policy.allow = {"read_file", "web_search"};
    auto result = ToolRegistry::applyToolPolicy(ALL_TOOLS, policy);
    CHECK(result == std::vector<std::string>{"read_file", "web_search"});
}

TEST_CASE("deny takes precedence over allow")
{
    ToolPolicy policy;
    policy.allow = {"read_file", "exec"};
    policy.deny = {"exec"};
    auto result = ToolRegistry::applyToolPolicy(ALL_TOOLS, policy);
    CHECK(result == std::vector<std::string>{"read_file"});
}

TEST_CASE("matching is case-insensitive")
{
    ToolPolicy policy;
    policy.deny = {"EXEC"};
    auto result = ToolRegistry::applyToolPolicy(ALL_TOOLS, policy);
    CHECK(result == std::vector<std::string>{"read_file", "write_file", "web_search"});
}
