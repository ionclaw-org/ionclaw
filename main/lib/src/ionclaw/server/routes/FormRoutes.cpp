#include "ionclaw/server/Routes.hpp"

namespace ionclaw
{
namespace server
{

nlohmann::json Routes::buildFormSchemas()
{
    using json = nlohmann::json;

    json schemas = json::object();

    schemas["bot"] = json::array({
        {{"name", "name"}, {"type", "text"}, {"label", "Bot Name"}, {"required", true}},
        {{"name", "description"}, {"type", "text"}, {"label", "Description"}},
    });

    schemas["server"] = json::array({
        {{"name", "host"}, {"type", "text"}, {"label", "Host"}, {"placeholder", "0.0.0.0"}},
        {{"name", "port"}, {"type", "int"}, {"label", "Port"}},
        {{"name", "public_url"}, {"type", "text"}, {"label", "Public URL"}, {"placeholder", "Leave empty for http://localhost:PORT"}},
        {{"name", "credential"}, {"type", "credential"}, {"label", "JWT Secret (Credential)"}},
    });

    schemas["web_client"] = json::array({
        {{"name", "credential"}, {"type", "credential"}, {"label", "Credential"}},
    });

    schemas["storage"] = json::array({
        {{"name", "type"}, {"type", "select"}, {"label", "Storage Type"}, {"options", json::array({
                                                                                          {{"label", "Local"}, {"value", "local"}},
                                                                                      })}},
    });

    schemas["tools_general"] = json::array({
        {{"name", "restrict_to_workspace"}, {"type", "bool"}, {"label", "Restrict to Workspace"}},
    });

    schemas["tools_exec"] = json::array({
        {{"name", "timeout"}, {"type", "int"}, {"label", "Timeout (seconds)"}},
    });

    schemas["tools_web_search"] = json::array({
        {{"name", "provider"}, {"type", "select"}, {"label", "Search Provider"}, {"options", json::array({
                                                                                                 {{"label", "Brave"}, {"value", "brave"}},
                                                                                                 {{"label", "DuckDuckGo"}, {"value", "duckduckgo"}},
                                                                                             })}},
        {{"name", "credential"}, {"type", "credential"}, {"label", "Credential (reference)"}},
        {{"name", "max_results"}, {"type", "int"}, {"label", "Max Results (1-10)"}},
    });

    schemas["channels_telegram"] = json::array({
        {{"name", "enabled"}, {"type", "bool"}, {"label", "Enabled"}},
        {{"name", "credential"}, {"type", "credential"}, {"label", "Credential"}},
        {{"name", "allowed_users"}, {"type", "text"}, {"label", "Allowed Users (comma-separated)"}},
        {{"name", "proxy"}, {"type", "text"}, {"label", "Proxy URL"}},
        {{"name", "reply_to_message"}, {"type", "bool"}, {"label", "Reply to Message"}},
    });

    schemas["channels_mcp"] = json::array({
        {{"name", "enabled"}, {"type", "bool"}, {"label", "Enabled"}},
        {{"name", "require_auth"}, {"type", "bool"}, {"label", "Require Token Authentication"}},
        {{"name", "credential"}, {"type", "credential"}, {"label", "Token Credential"}, {"visible_when", {{"require_auth", true}}}},
    });

    schemas["image"] = json::array({
        {{"name", "model"}, {"type", "text"}, {"label", "Model"}, {"placeholder", "openai/dall-e-3"}},
        {{"name", "aspect_ratio"}, {"type", "text"}, {"label", "Aspect Ratio"}, {"placeholder", "16:9"}},
        {{"name", "size"}, {"type", "text"}, {"label", "Size"}, {"placeholder", "1024x1024"}},
    });

    schemas["transcription"] = json::array({
        {{"name", "model"}, {"type", "text"}, {"label", "Model"}, {"placeholder", "openai/whisper-1"}},
    });

    schemas["agent"] = json::array({
        {{"name", "workspace"}, {"type", "text"}, {"label", "Workspace"}},
        {{"name", "model"}, {"type", "text"}, {"label", "Model"}, {"required", true}},
        {{"name", "description"}, {"type", "text"}, {"label", "Description"}},
        {{"name", "instructions"}, {"type", "long_text"}, {"label", "Instructions"}},
    });

    schemas["provider"] = json::array({
        {{"name", "credential"}, {"type", "credential"}, {"label", "Credential"}, {"required", true}},
        {{"name", "base_url"}, {"type", "text"}, {"label", "Base URL"}, {"placeholder", "Leave empty to use default endpoint"}},
    });

    schemas["credential"] = json::array({
        {{"name", "type"}, {"type", "select"}, {"label", "Type"}, {"required", true}, {"options", json::array({
                                                                                                      {{"label", "Simple (API Key / Token)"}, {"value", "simple"}},
                                                                                                      {{"label", "OAuth 1.0"}, {"value", "oauth1"}},
                                                                                                      {{"label", "Basic Auth"}, {"value", "basic"}},
                                                                                                      {{"label", "Login"}, {"value", "login"}},
                                                                                                      {{"label", "Header"}, {"value", "header"}},
                                                                                                      {{"label", "Bearer"}, {"value", "bearer"}},
                                                                                                  })}},
        {{"name", "key"}, {"type", "secret"}, {"label", "Key"}, {"generate", true}, {"visible_when", {{"type", "simple"}}}},
        {{"name", "consumer_key"}, {"type", "secret"}, {"label", "Consumer Key"}, {"placeholder", "Leave empty to keep current"}, {"visible_when", {{"type", "oauth1"}}}},
        {{"name", "consumer_secret"}, {"type", "secret"}, {"label", "Consumer Secret"}, {"placeholder", "Leave empty to keep current"}, {"visible_when", {{"type", "oauth1"}}}},
        {{"name", "access_token"}, {"type", "secret"}, {"label", "Access Token"}, {"placeholder", "Leave empty to keep current"}, {"visible_when", {{"type", "oauth1"}}}},
        {{"name", "access_token_secret"}, {"type", "secret"}, {"label", "Access Token Secret"}, {"placeholder", "Leave empty to keep current"}, {"visible_when", {{"type", "oauth1"}}}},
        {{"name", "username"}, {"type", "text"}, {"label", "Username"}, {"visible_when", {{"type", json::array({"basic", "login"})}}}},
        {{"name", "password"}, {"type", "secret"}, {"label", "Password"}, {"visible_when", {{"type", json::array({"basic", "login"})}}}},
        {{"name", "header_name"}, {"type", "text"}, {"label", "Header Name"}, {"placeholder", "e.g. Authorization, X-API-Key"}, {"visible_when", {{"type", "header"}}}},
        {{"name", "value"}, {"type", "secret"}, {"label", "Value"}, {"placeholder", "Leave empty to keep current"}, {"visible_when", {{"type", "header"}}}},
        {{"name", "token"}, {"type", "secret"}, {"label", "Token"}, {"placeholder", "Leave empty to keep current"}, {"visible_when", {{"type", "bearer"}}}},
    });

    schemas["login"] = json::array({
        {{"name", "username"}, {"type", "text"}, {"label", "Username"}, {"required", true}},
        {{"name", "password"}, {"type", "secret"}, {"label", "Password"}, {"required", true}},
    });

    schemas["scheduler_job"] = json::array({
        {{"name", "name"}, {"type", "text"}, {"label", "Name"}, {"required", true}},
        {{"name", "message"}, {"type", "text"}, {"label", "Message"}, {"required", true}},
        {{"name", "type"}, {"type", "select"}, {"label", "Schedule Type"}, {"options", json::array({
                                                                                           {{"label", "Interval"}, {"value", "every"}},
                                                                                           {{"label", "Cron Expression"}, {"value", "cron"}},
                                                                                           {{"label", "One-time"}, {"value", "at"}},
                                                                                       })}},
        {{"name", "every_seconds"}, {"type", "int"}, {"label", "Interval (seconds)"}, {"visible_when", {{"type", "every"}}}},
        {{"name", "cron_expr"}, {"type", "text"}, {"label", "Cron Expression"}, {"placeholder", "0 9 * * *"}, {"visible_when", {{"type", "cron"}}}},
        {{"name", "at"}, {"type", "datetime"}, {"label", "Date/Time"}, {"visible_when", {{"type", "at"}}}},
    });

    schemas["create_file"] = json::array({
        {{"name", "name"}, {"type", "text"}, {"label", "File Name"}, {"required", true}},
    });

    schemas["create_folder"] = json::array({
        {{"name", "name"}, {"type", "text"}, {"label", "Folder Name"}, {"required", true}},
    });

    return schemas;
}

void Routes::handleFormsList(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp)
{
    static auto schemas = buildFormSchemas();
    sendJson(resp, schemas);
}

} // namespace server
} // namespace ionclaw
