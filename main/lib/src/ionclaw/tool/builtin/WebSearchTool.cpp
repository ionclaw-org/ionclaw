#include "ionclaw/tool/builtin/WebSearchTool.hpp"

#include "ionclaw/config/Config.hpp"
#include "ionclaw/search/SearchProviderRegistry.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

ToolResult WebSearchTool::execute(const nlohmann::json &params, const ToolContext &context)
{
    std::string query = params.at("query").get<std::string>();

    // parse optional result count, default from config
    int defaultCount = (context.config) ? context.config->tools.webSearchMaxResults : 5;
    int count = defaultCount;

    if (params.contains("count") && params["count"].is_number_integer())
    {
        count = std::max(1, std::min(10, params["count"].get<int>()));
    }

    if (!context.config)
    {
        return "Error: configuration not available";
    }

    // resolve search provider
    std::string providerName = context.config->tools.webSearchProvider;

    if (providerName.empty())
    {
        providerName = "brave";
    }

    search::SearchProvider *provider = search::SearchProviderRegistry::instance().get(providerName);

    if (!provider)
    {
        return "Error: unknown web search provider '" + providerName + "' (check tools.web_search_provider in config.yml)";
    }

    // resolve credential
    std::string credentialName = context.config->tools.webSearchCredential;

    if (credentialName.empty())
    {
        return "Error: web search credential not configured (tools.web_search_credential in config.yml)";
    }

    auto credIt = context.config->credentials.find(credentialName);

    if (credIt == context.config->credentials.end())
    {
        return "Error: credential '" + credentialName + "' not found in credentials config";
    }

    return provider->search(query, count, credIt->second);
}

ToolSchema WebSearchTool::schema() const
{
    return {
        "web_search",
        "Search the web. Returns titles, URLs, and descriptions. Provider is configured in settings (e.g. brave, duckduckgo).",
        {{"type", "object"},
         {"properties",
          {{"query", {{"type", "string"}, {"description", "The search query"}}},
           {"count", {{"type", "integer"}, {"description", "Number of results (1-10, default 5)"}, {"minimum", 1}, {"maximum", 10}}}}},
         {"required", nlohmann::json::array({"query"})}}};
}

} // namespace builtin
} // namespace tool
} // namespace ionclaw
