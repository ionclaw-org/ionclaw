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
    // pre-compiled regex objects (thread_local for thread safety)
    thread_local static const std::regex reScript(R"(<script[^>]*>[\s\S]*?</script>)", std::regex::icase);
    thread_local static const std::regex reStyle(R"(<style[^>]*>[\s\S]*?</style>)", std::regex::icase);
    thread_local static const std::regex reNav(R"(<nav[^>]*>[\s\S]*?</nav>)", std::regex::icase);
    thread_local static const std::regex reFooter(R"(<footer[^>]*>[\s\S]*?</footer>)", std::regex::icase);
    thread_local static const std::regex reLink(R"RE(<a\s[^>]*href\s*=\s*"([^"]*)"[^>]*>([\s\S]*?)</a>)RE", std::regex::icase);
    thread_local static const std::regex reH1(R"(<h1[^>]*>([\s\S]*?)</h1>)", std::regex::icase);
    thread_local static const std::regex reH2(R"(<h2[^>]*>([\s\S]*?)</h2>)", std::regex::icase);
    thread_local static const std::regex reH3(R"(<h3[^>]*>([\s\S]*?)</h3>)", std::regex::icase);
    thread_local static const std::regex reH4(R"(<h4[^>]*>([\s\S]*?)</h4>)", std::regex::icase);
    thread_local static const std::regex reH5(R"(<h5[^>]*>([\s\S]*?)</h5>)", std::regex::icase);
    thread_local static const std::regex reH6(R"(<h6[^>]*>([\s\S]*?)</h6>)", std::regex::icase);
    thread_local static const std::regex reLi(R"(<li[^>]*>)", std::regex::icase);
    thread_local static const std::regex reBlock(R"(<(br|p|div|tr|blockquote|pre|hr|ul|ol|table|section|article|header|main)[^>]*>)", std::regex::icase);
    thread_local static const std::regex reBold(R"(<(b|strong)[^>]*>([\s\S]*?)</\1>)", std::regex::icase);
    thread_local static const std::regex reItalic(R"(<(i|em)[^>]*>([\s\S]*?)</\1>)", std::regex::icase);
    thread_local static const std::regex reCode(R"(<code[^>]*>([\s\S]*?)</code>)", std::regex::icase);
    thread_local static const std::regex reTag(R"(<[^>]+>)");
    thread_local static const std::regex reAmp(R"(&amp;)");
    thread_local static const std::regex reLt(R"(&lt;)");
    thread_local static const std::regex reGt(R"(&gt;)");
    thread_local static const std::regex reQuot(R"(&quot;)");
    thread_local static const std::regex reApos(R"(&#39;|&apos;)");
    thread_local static const std::regex reNbsp(R"(&nbsp;)");
    thread_local static const std::regex reSpaces(R"([ \t]+)");
    thread_local static const std::regex reLeadingSpaces(R"(\n[ \t]+)");
    thread_local static const std::regex reMultiNewlines(R"(\n{3,})");

    std::string result = html;

    // remove script, style, nav, and footer blocks
    result = std::regex_replace(result, reScript, "");
    result = std::regex_replace(result, reStyle, "");
    result = std::regex_replace(result, reNav, "");
    result = std::regex_replace(result, reFooter, "");

    // convert links to markdown: <a href="url">text</a> → [text](url)
    result = std::regex_replace(result, reLink, "[$2]($1)");

    // convert headings to markdown
    result = std::regex_replace(result, reH1, "\n# $1\n");
    result = std::regex_replace(result, reH2, "\n## $1\n");
    result = std::regex_replace(result, reH3, "\n### $1\n");
    result = std::regex_replace(result, reH4, "\n#### $1\n");
    result = std::regex_replace(result, reH5, "\n##### $1\n");
    result = std::regex_replace(result, reH6, "\n###### $1\n");

    // convert list items to markdown
    result = std::regex_replace(result, reLi, "\n- ");

    // block elements → newlines
    result = std::regex_replace(result, reBlock, "\n");

    // bold and italic
    result = std::regex_replace(result, reBold, "**$2**");
    result = std::regex_replace(result, reItalic, "*$2*");

    // code blocks
    result = std::regex_replace(result, reCode, "`$1`");

    // remove all remaining tags
    result = std::regex_replace(result, reTag, "");

    // unescape HTML entities
    result = std::regex_replace(result, reAmp, "&");
    result = std::regex_replace(result, reLt, "<");
    result = std::regex_replace(result, reGt, ">");
    result = std::regex_replace(result, reQuot, "\"");
    result = std::regex_replace(result, reApos, "'");
    result = std::regex_replace(result, reNbsp, " ");

    // collapse whitespace
    result = std::regex_replace(result, reSpaces, " ");
    result = std::regex_replace(result, reLeadingSpaces, "\n");
    result = std::regex_replace(result, reMultiNewlines, "\n\n");

    // trim
    auto start = result.find_first_not_of(" \t\n\r");
    auto end = result.find_last_not_of(" \t\n\r");

    if (start == std::string::npos)
    {
        return "";
    }

    return result.substr(start, end - start + 1);
}

ToolResult WebFetchTool::execute(const nlohmann::json &params, const ToolContext &context)
{
    auto url = params.at("url").get<std::string>();
    int maxChars = 50000;

    if (params.contains("max_chars") && params["max_chars"].is_number_integer())
    {
        maxChars = std::max(1000, params["max_chars"].get<int>());
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
            // json: pretty-print
            try
            {
                auto json = nlohmann::json::parse(response.body);
                text = json.dump(2);
            }
            catch (const nlohmann::json::parse_error &)
            {
                text = response.body;
            }

            extractor = "json";
        }
        else if (contentType.find("text/html") != std::string::npos)
        {
            // html: strip tags to readable text
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

        if (text.size() > static_cast<size_t>(maxChars))
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
            {"length", static_cast<int64_t>(text.size())},
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
