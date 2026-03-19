#include "ionclaw/search/DuckDuckGoSearchProvider.hpp"

#include <algorithm>
#include <sstream>

#include "ionclaw/util/HttpClient.hpp"
#include "ionclaw/util/StringHelper.hpp"
#include "nlohmann/json.hpp"

namespace ionclaw
{
namespace search
{

std::string DuckDuckGoSearchProvider::name() const
{
    return "duckduckgo";
}

std::string DuckDuckGoSearchProvider::search(const std::string &query, int count, const ionclaw::config::CredentialConfig &credential) const
{
    (void)credential;

    std::string url = "https://api.duckduckgo.com/?q=" + ionclaw::util::StringHelper::urlEncode(query) + "&format=json";

    std::map<std::string, std::string> headers = {{"Accept", "application/json"}};

    auto response = ionclaw::util::HttpClient::request("GET", url, headers);

    if (response.statusCode != 200)
    {
        return "Error: DuckDuckGo API returned HTTP " + std::to_string(response.statusCode) +
               ": " + ionclaw::util::StringHelper::utf8SafeTruncate(response.body, 500);
    }

    auto json = nlohmann::json::parse(response.body, nullptr, false);

    if (json.is_discarded())
    {
        return "Error: DuckDuckGo API returned invalid JSON";
    }

    std::string abstractText = json.value("AbstractText", "");
    std::string abstractUrl = json.value("AbstractURL", "");
    auto related = json.value("RelatedTopics", nlohmann::json::array());

    std::ostringstream output;
    int idx = 1;
    if (!abstractText.empty())
    {
        output << idx << ". " << json.value("Heading", "Result") << "\n"
               << "   " << abstractText << "\n";
        if (!abstractUrl.empty())
        {
            output << "   URL: " << abstractUrl << "\n";
        }
        output << "\n";
        ++idx;
    }

    int limit = count > 0 ? std::min(count, 10) : 5;
    for (const auto &topic : related)
    {
        if (idx > limit)
        {
            break;
        }
        std::string text = topic.value("Text", "");
        std::string firstUrl = topic.value("FirstURL", "");
        if (text.empty())
        {
            continue;
        }
        output << idx << ". " << text << "\n";
        if (!firstUrl.empty())
        {
            output << "   URL: " << firstUrl << "\n";
        }
        output << "\n";
        ++idx;
    }

    std::string result = output.str();
    if (result.empty())
    {
        return "No results found for: " + query;
    }
    return result;
}

} // namespace search
} // namespace ionclaw
