#include "ionclaw/search/DuckDuckGoSearchProvider.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "ionclaw/util/HttpClient.hpp"
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
    std::ostringstream encoded;
    for (char c : query)
    {
        if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            encoded << c;
        }
        else if (c == ' ')
        {
            encoded << '+';
        }
        else
        {
            encoded << '%' << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                    << static_cast<int>(static_cast<unsigned char>(c));
        }
    }

    std::string url = "https://api.duckduckgo.com/?q=" + encoded.str() + "&format=json";

    std::map<std::string, std::string> headers = {{"Accept", "application/json"}};

    auto response = ionclaw::util::HttpClient::request("GET", url, headers);

    if (response.statusCode != 200)
    {
        return "Error: DuckDuckGo API returned HTTP " + std::to_string(response.statusCode) +
               ": " + response.body.substr(0, 500);
    }

    auto json = nlohmann::json::parse(response.body);
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
