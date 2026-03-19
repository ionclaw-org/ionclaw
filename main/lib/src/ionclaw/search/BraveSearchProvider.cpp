#include "ionclaw/search/BraveSearchProvider.hpp"

#include <sstream>

#include "ionclaw/util/HttpClient.hpp"
#include "ionclaw/util/StringHelper.hpp"
#include "nlohmann/json.hpp"

namespace ionclaw
{
namespace search
{

std::string BraveSearchProvider::name() const
{
    return "brave";
}

std::string BraveSearchProvider::search(const std::string &query, int count, const ionclaw::config::CredentialConfig &credential) const
{
    std::string token = credential.key.empty() ? credential.token : credential.key;
    if (token.empty())
    {
        return "Error: Brave credential must have key or token (tools.web_search credential in config.yml)";
    }

    std::string url = "https://api.search.brave.com/res/v1/web/search?q=" + ionclaw::util::StringHelper::urlEncode(query) +
                      "&count=" + std::to_string(count);

    std::map<std::string, std::string> headers = {
        {"Accept", "application/json"},
        {"X-Subscription-Token", token},
    };

    auto response = ionclaw::util::HttpClient::request("GET", url, headers);

    if (response.statusCode != 200)
    {
        return "Error: Brave Search API returned HTTP " + std::to_string(response.statusCode) +
               ": " + ionclaw::util::StringHelper::utf8SafeTruncate(response.body, 500);
    }

    auto json = nlohmann::json::parse(response.body, nullptr, false);

    if (json.is_discarded())
    {
        return "Error: Brave Search API returned invalid JSON";
    }

    auto results = json.value("web", nlohmann::json::object()).value("results", nlohmann::json::array());

    if (results.empty())
    {
        return "No results found for: " + query;
    }

    std::ostringstream output;
    int idx = 1;
    for (const auto &result : results)
    {
        std::string title = result.value("title", "");
        std::string resultUrl = result.value("url", "");
        std::string description = result.value("description", "");
        output << idx << ". " << title << "\n"
               << "   URL: " << resultUrl << "\n"
               << "   " << description << "\n\n";
        ++idx;
    }
    return output.str();
}

} // namespace search
} // namespace ionclaw
