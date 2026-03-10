#pragma once

#include <map>
#include <string>
#include <vector>

#include "ionclaw/tool/Tool.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

class HttpClientTool : public Tool
{
public:
    ToolResult execute(const nlohmann::json &params, const ToolContext &context) override;
    ToolSchema schema() const override;

private:
    // constants
    static const size_t MAX_RESPONSE_BYTES;

    // oauth1 helpers
    static std::string percentEncode(const std::string &value);
    static std::string generateNonce();
    static std::string buildOAuth1Header(
        const std::string &method,
        const std::string &url,
        const std::string &consumerKey,
        const std::string &consumerSecret,
        const std::string &accessToken,
        const std::string &tokenSecret,
        const std::vector<std::pair<std::string, std::string>> &bodyParams = {});

    // auth profile application
    static void applyAuth(
        const std::string &profileName,
        const ionclaw::config::Config &config,
        const std::string &method,
        const std::string &url,
        std::map<std::string, std::string> &headers,
        const std::string &body = "",
        const std::string &contentType = "json");
};

} // namespace builtin
} // namespace tool
} // namespace ionclaw
