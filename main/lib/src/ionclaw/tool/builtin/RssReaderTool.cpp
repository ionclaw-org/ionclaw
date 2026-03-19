#include "ionclaw/tool/builtin/RssReaderTool.hpp"

#include <regex>
#include <sstream>

#include "Poco/DOM/AutoPtr.h"
#include "Poco/DOM/DOMParser.h"
#include "Poco/DOM/Document.h"
#include "Poco/DOM/NodeList.h"
#include "Poco/SAX/InputSource.h"

#include "ionclaw/util/HttpClient.hpp"
#include "ionclaw/util/SsrfGuard.hpp"
#include "ionclaw/util/StringHelper.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

// extract text content from a child element by tag name
std::string RssReaderTool::getElementText(Poco::XML::Element *parent, const std::string &tagName)
{
    if (!parent)
    {
        return "";
    }

    auto nodes = parent->getElementsByTagName(tagName);

    if (nodes->length() == 0)
    {
        return "";
    }

    auto element = static_cast<Poco::XML::Element *>(nodes->item(0));
    return element ? element->innerText() : "";
}

// strip html tags and unescape basic entities
std::string RssReaderTool::stripHtmlTags(const std::string &html)
{
    // thread_local to avoid data race on concurrent std::regex_search calls
    thread_local static const std::regex tagRegex(R"(<[^>]+>)");
    thread_local static const std::regex ampRegex(R"(&amp;)");
    thread_local static const std::regex ltRegex(R"(&lt;)");
    thread_local static const std::regex gtRegex(R"(&gt;)");

    auto result = std::regex_replace(html, tagRegex, "");
    result = std::regex_replace(result, ampRegex, "&");
    result = std::regex_replace(result, ltRegex, "<");
    result = std::regex_replace(result, gtRegex, ">");

    // trim whitespace
    auto start = result.find_first_not_of(" \t\n\r");
    auto end = result.find_last_not_of(" \t\n\r");

    if (start == std::string::npos)
    {
        return "";
    }

    return result.substr(start, end - start + 1);
}

// execute rss/atom feed fetch and parse
ToolResult RssReaderTool::execute(const nlohmann::json &params, const ToolContext &context)
{
    auto url = params.value("url", std::string(""));

    if (url.empty())
    {
        return "Error: url is required";
    }

    int count = 10;

    if (params.contains("count") && params["count"].is_number_integer())
    {
        count = std::max(1, std::min(50, params["count"].get<int>()));
    }

    // ssrf validation
    try
    {
        ionclaw::util::SsrfGuard::validateUrl(url);
    }
    catch (const std::exception &e)
    {
        return "Error: " + std::string(e.what());
    }

    try
    {
        // fetch feed
        auto response = ionclaw::util::HttpClient::request(
            "GET", url, {{"User-Agent", "IonClaw/1.0 RSS Reader"}}, "", 30, true,
            ionclaw::util::SsrfGuard::validateUrl);

        if (response.statusCode != 200)
        {
            return "Error: HTTP " + std::to_string(response.statusCode) + " fetching feed";
        }

        // xml bomb protection
        if (response.body.size() > MAX_FEED_SIZE)
        {
            return "Error: feed too large (max 2MB)";
        }

        // parse xml
        Poco::XML::DOMParser parser;
        parser.setFeature(Poco::XML::DOMParser::FEATURE_FILTER_WHITESPACE, true);

        std::istringstream xmlStream(response.body);
        Poco::XML::InputSource source(xmlStream);
        auto doc = parser.parse(&source);

        nlohmann::json entries = nlohmann::json::array();

        // try rss 2.0 format first (channel/item)
        auto items = doc->getElementsByTagName("item");

        if (items->length() > 0)
        {
            for (unsigned long i = 0; i < items->length() && static_cast<int>(i) < count; ++i)
            {
                auto item = static_cast<Poco::XML::Element *>(items->item(i));
                auto title = getElementText(item, "title");
                auto link = getElementText(item, "link");
                auto pubDate = getElementText(item, "pubDate");
                auto description = getElementText(item, "description");

                auto summary = stripHtmlTags(description);

                if (static_cast<int>(summary.size()) > MAX_SUMMARY_CHARS)
                {
                    summary = ionclaw::util::StringHelper::utf8SafeTruncate(summary, MAX_SUMMARY_CHARS) + "...";
                }

                entries.push_back({
                    {"title", title},
                    {"link", link},
                    {"published", pubDate},
                    {"summary", summary},
                });
            }
        }
        else
        {
            // try atom format (entry)
            auto atomEntries = doc->getElementsByTagName("entry");

            for (unsigned long i = 0; i < atomEntries->length() && static_cast<int>(i) < count; ++i)
            {
                auto entry = static_cast<Poco::XML::Element *>(atomEntries->item(i));
                auto title = getElementText(entry, "title");
                auto published = getElementText(entry, "published");

                if (published.empty())
                {
                    published = getElementText(entry, "updated");
                }

                auto summary = getElementText(entry, "summary");

                if (summary.empty())
                {
                    summary = getElementText(entry, "content");
                }

                summary = stripHtmlTags(summary);

                if (static_cast<int>(summary.size()) > MAX_SUMMARY_CHARS)
                {
                    summary = ionclaw::util::StringHelper::utf8SafeTruncate(summary, MAX_SUMMARY_CHARS) + "...";
                }

                // get link from link element's href attribute
                std::string link;
                auto linkNodes = entry->getElementsByTagName("link");

                if (linkNodes->length() > 0)
                {
                    auto linkElem = static_cast<Poco::XML::Element *>(linkNodes->item(0));

                    if (linkElem)
                    {
                        link = linkElem->getAttribute("href");

                        if (link.empty())
                        {
                            link = linkElem->innerText();
                        }
                    }
                }

                entries.push_back({
                    {"title", title},
                    {"link", link},
                    {"published", published},
                    {"summary", summary},
                });
            }
        }

        // build result
        if (entries.empty())
        {
            return "No entries found in the feed at: " + url;
        }

        nlohmann::json result = {
            {"url", url},
            {"count", entries.size()},
            {"entries", entries},
        };

        return result.dump(2);
    }
    catch (const std::exception &e)
    {
        return "Error: failed to parse feed: " + std::string(e.what());
    }
}

// schema definition
ToolSchema RssReaderTool::schema() const
{
    return {
        "rss_reader",
        "Read and parse RSS or Atom feeds. Returns titles, links, dates, and summaries.",
        {{"type", "object"},
         {"properties",
          {{"url", {{"type", "string"}, {"description", "The RSS or Atom feed URL"}}},
           {"count", {{"type", "integer"}, {"description", "Maximum number of entries to return (1-50, default 10)"}, {"minimum", 1}, {"maximum", 50}}}}},
         {"required", nlohmann::json::array({"url"})}}};
}

} // namespace builtin
} // namespace tool
} // namespace ionclaw
