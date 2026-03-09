#include "ionclaw/tool/builtin/WebFetchTool.hpp"

#include <regex>
#include <sstream>

#include "ionclaw/util/HttpClient.hpp"
#include "ionclaw/util/SsrfGuard.hpp"
#include "ionclaw/util/StringHelper.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

// convert HTML to readable markdown-like text
std::string WebFetchTool::stripHtml(const std::string &html)
{
    std::string result = html;

    // remove script, style, nav, and footer blocks
    result = std::regex_replace(result, std::regex(R"(<script[^>]*>[\s\S]*?</script>)", std::regex::icase), "");
    result = std::regex_replace(result, std::regex(R"(<style[^>]*>[\s\S]*?</style>)", std::regex::icase), "");
    result = std::regex_replace(result, std::regex(R"(<nav[^>]*>[\s\S]*?</nav>)", std::regex::icase), "");
    result = std::regex_replace(result, std::regex(R"(<footer[^>]*>[\s\S]*?</footer>)", std::regex::icase), "");

    // convert links to markdown: <a href="url">text</a> → [text](url)
    result = std::regex_replace(result,
                                std::regex(R"RE(<a\s[^>]*href\s*=\s*"([^"]*)"[^>]*>([\s\S]*?)</a>)RE", std::regex::icase),
                                "[$2]($1)");

    // convert headings to markdown: <h1>text</h1> → \n# text\n
    result = std::regex_replace(result, std::regex(R"(<h1[^>]*>([\s\S]*?)</h1>)", std::regex::icase), "\n# $1\n");
    result = std::regex_replace(result, std::regex(R"(<h2[^>]*>([\s\S]*?)</h2>)", std::regex::icase), "\n## $1\n");
    result = std::regex_replace(result, std::regex(R"(<h3[^>]*>([\s\S]*?)</h3>)", std::regex::icase), "\n### $1\n");
    result = std::regex_replace(result, std::regex(R"(<h4[^>]*>([\s\S]*?)</h4>)", std::regex::icase), "\n#### $1\n");
    result = std::regex_replace(result, std::regex(R"(<h5[^>]*>([\s\S]*?)</h5>)", std::regex::icase), "\n##### $1\n");
    result = std::regex_replace(result, std::regex(R"(<h6[^>]*>([\s\S]*?)</h6>)", std::regex::icase), "\n###### $1\n");

    // convert list items to markdown
    result = std::regex_replace(result, std::regex(R"(<li[^>]*>)", std::regex::icase), "\n- ");

    // block elements → newlines
    result = std::regex_replace(result,
                                std::regex(R"(<(br|p|div|tr|blockquote|pre|hr|ul|ol|table|section|article|header|main)[^>]*>)", std::regex::icase),
                                "\n");

    // bold and italic
    result = std::regex_replace(result, std::regex(R"(<(b|strong)[^>]*>([\s\S]*?)</\1>)", std::regex::icase), "**$2**");
    result = std::regex_replace(result, std::regex(R"(<(i|em)[^>]*>([\s\S]*?)</\1>)", std::regex::icase), "*$2*");

    // code blocks
    result = std::regex_replace(result, std::regex(R"(<code[^>]*>([\s\S]*?)</code>)", std::regex::icase), "`$1`");

    // remove all remaining tags
    result = std::regex_replace(result, std::regex(R"(<[^>]+>)"), "");

    // unescape HTML entities
    result = std::regex_replace(result, std::regex(R"(&amp;)"), "&");
    result = std::regex_replace(result, std::regex(R"(&lt;)"), "<");
    result = std::regex_replace(result, std::regex(R"(&gt;)"), ">");
    result = std::regex_replace(result, std::regex(R"(&quot;)"), "\"");
    result = std::regex_replace(result, std::regex(R"(&#39;|&apos;)"), "'");
    result = std::regex_replace(result, std::regex(R"(&nbsp;)"), " ");

    // collapse whitespace
    result = std::regex_replace(result, std::regex(R"([ \t]+)"), " ");
    result = std::regex_replace(result, std::regex(R"(\n[ \t]+)"), "\n");
    result = std::regex_replace(result, std::regex(R"(\n{3,})"), "\n\n");

    // trim
    auto start = result.find_first_not_of(" \t\n\r");
    auto end = result.find_last_not_of(" \t\n\r");

    if (start == std::string::npos)
    {
        return "";
    }

    return result.substr(start, end - start + 1);
}

std::string WebFetchTool::execute(const nlohmann::json &params, const ToolContext &context)
{
    auto url = params.at("url").get<std::string>();
    int maxChars = 50000;

    if (params.contains("max_chars") && params["max_chars"].is_number_integer())
    {
        maxChars = std::max(1000, params["max_chars"].get<int>());
    }

    // SSRF validation
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
        auto response = ionclaw::util::HttpClient::request(
            "GET", url, {{"User-Agent", "Mozilla/5.0 (Macintosh; Intel Mac OS X 14_7_2) AppleWebKit/537.36"}}, "", 30, true,
            ionclaw::util::SsrfGuard::validateUrl);

        auto finalUrl = response.headers.count("X-Final-URL")
                            ? response.headers.at("X-Final-URL")
                            : url;

        if (response.statusCode < 200 || response.statusCode >= 400)
        {
            return "Error: HTTP " + std::to_string(response.statusCode) +
                   " fetching " + url;
        }

        std::string text;
        std::string extractor;
        auto contentType = response.headers.count("Content-Type")
                               ? response.headers.at("Content-Type")
                               : "";

        if (contentType.find("application/json") != std::string::npos)
        {
            // JSON: pretty-print
            try
            {
                auto json = nlohmann::json::parse(response.body);
                text = json.dump(2);
            }
            catch (...)
            {
                text = response.body;
            }

            extractor = "json";
        }
        else if (contentType.find("text/html") != std::string::npos)
        {
            // HTML: strip tags to readable text
            text = stripHtml(response.body);
            extractor = "html";
        }
        else
        {
            // raw text
            text = response.body;
            extractor = "raw";
        }

        bool truncated = false;

        if (static_cast<int>(text.size()) > maxChars)
        {
            text = ionclaw::util::StringHelper::utf8SafeTruncate(text, maxChars);
            truncated = true;
        }

        nlohmann::json result = {
            {"url", url},
            {"finalUrl", finalUrl},
            {"status", response.statusCode},
            {"extractor", extractor},
            {"truncated", truncated},
            {"length", static_cast<int>(text.size())},
            {"text", text},
        };

        return result.dump(2);
    }
    catch (const std::exception &e)
    {
        return "Error: failed to fetch URL: " + std::string(e.what());
    }
}

ToolSchema WebFetchTool::schema() const
{
    return {
        "web_fetch",
        "Fetch the content of a URL. Extracts readable text from HTML, pretty-prints JSON, or returns raw text.",
        {{"type", "object"},
         {"properties",
          {{"url", {{"type", "string"}, {"description", "The URL to fetch"}}},
           {"max_chars", {{"type", "integer"}, {"description", "Maximum characters to return (default 50000)"}}}}},
         {"required", nlohmann::json::array({"url"})}}};
}

} // namespace builtin
} // namespace tool
} // namespace ionclaw
