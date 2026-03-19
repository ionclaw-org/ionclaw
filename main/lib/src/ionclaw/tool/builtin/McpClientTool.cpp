#include "ionclaw/tool/builtin/McpClientTool.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <sstream>

#include "spdlog/spdlog.h"

#include "ionclaw/util/HttpClient.hpp"
#include "ionclaw/util/SsrfGuard.hpp"
#include "ionclaw/util/StringHelper.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

// thread-safe monotonic request ID generator
int64_t McpClientTool::nextRequestId()
{
    static std::atomic<int64_t> counter{0};
    return ++counter;
}

nlohmann::json McpClientTool::sendRpcRequest(
    const std::string &url,
    const nlohmann::json &request,
    const std::string &sessionId,
    const std::string &authToken,
    int timeout)
{
    std::map<std::string, std::string> headers;
    headers["Content-Type"] = "application/json";
    headers["Accept"] = "application/json, text/event-stream";

    if (!sessionId.empty())
    {
        headers["MCP-Session-Id"] = sessionId;
    }

    if (!authToken.empty())
    {
        headers["Authorization"] = "Bearer " + authToken;
    }

    auto response = ionclaw::util::HttpClient::request(
        "POST", url, headers, request.dump(), timeout, false);

    if (response.statusCode == 404)
    {
        throw std::runtime_error(
            "MCP session not found (HTTP 404). The session may have expired — call initialize again to start a new session.");
    }

    if (response.statusCode < 200 || response.statusCode >= 300)
    {
        throw std::runtime_error(
            "MCP server returned HTTP " + std::to_string(response.statusCode) +
            ": " + ionclaw::util::StringHelper::utf8SafeTruncate(response.body, 500));
    }

    // extract relevant headers (case-insensitive per HTTP spec)
    auto contentType = std::string();
    auto responseSessionId = std::string();

    for (const auto &[key, value] : response.headers)
    {
        auto lower = key;
        ionclaw::util::StringHelper::toLowerInPlace(lower);

        if (lower == "content-type")
        {
            contentType = value;
        }
        else if (lower == "mcp-session-id")
        {
            responseSessionId = value;
        }
    }

    // handle SSE responses: extract the last json-rpc message from "data:" lines
    auto responseBody = response.body;

    if (responseBody.empty())
    {
        throw std::runtime_error("MCP server returned empty response body");
    }

    if (contentType.find("text/event-stream") != std::string::npos)
    {
        std::string lastData;
        std::istringstream stream(responseBody);
        std::string line;

        while (std::getline(stream, line))
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }

            if (line.rfind("data: ", 0) == 0)
            {
                lastData = line.substr(6);
            }
        }

        if (lastData.empty())
        {
            throw std::runtime_error("MCP server returned SSE response with no data");
        }

        responseBody = lastData;
    }

    auto body = nlohmann::json::parse(responseBody, nullptr, false);

    if (body.is_discarded())
    {
        throw std::runtime_error("MCP server returned invalid JSON response");
    }

    // propagate JSON-RPC errors
    if (body.contains("error") && body["error"].is_object())
    {
        auto code = body["error"].value("code", 0);
        auto message = body["error"].value("message", std::string("Unknown error"));
        throw std::runtime_error("MCP error " + std::to_string(code) + ": " + message);
    }

    if (!responseSessionId.empty())
    {
        body["_mcp_session_id"] = responseSessionId;
    }

    return body;
}

void McpClientTool::sendDeleteRequest(
    const std::string &url,
    const std::string &sessionId,
    const std::string &authToken,
    int timeout)
{
    std::map<std::string, std::string> headers;

    if (!sessionId.empty())
    {
        headers["MCP-Session-Id"] = sessionId;
    }

    if (!authToken.empty())
    {
        headers["Authorization"] = "Bearer " + authToken;
    }

    auto response = ionclaw::util::HttpClient::request("DELETE", url, headers, "", timeout, false);

    // 405 is expected if server doesn't support client-initiated termination (per MCP spec)
    if (response.statusCode != 405 && (response.statusCode < 200 || response.statusCode >= 300))
    {
        throw std::runtime_error(
            "MCP server returned HTTP " + std::to_string(response.statusCode) +
            " on DELETE: " + ionclaw::util::StringHelper::utf8SafeTruncate(response.body, 500));
    }
}

ToolResult McpClientTool::execute(const nlohmann::json &params, const ToolContext &)
{
    auto action = params.value("action", std::string(""));
    auto url = params.value("url", std::string(""));

    if (action.empty())
    {
        return "Error: action is required";
    }

    if (url.empty())
    {
        return "Error: url is required";
    }
    auto sessionId = params.value("session_id", std::string(""));
    auto authToken = params.value("auth_token", std::string(""));
    auto timeout = params.value("timeout", DEFAULT_TIMEOUT_SECONDS);

    // ssrf validation (allow loopback for local MCP servers)
    try
    {
        ionclaw::util::SsrfGuard::validateUrlAllowLoopback(url);
    }
    catch (const std::exception &e)
    {
        return "Error: " + std::string(e.what());
    }

    try
    {
        if (action == "initialize")
            return actionInitialize(url, authToken, timeout);
        if (action == "list_tools")
            return actionListTools(url, sessionId, authToken, timeout);
        if (action == "call_tool")
        {
            auto toolName = params.at("tool_name").get<std::string>();
            auto toolArgs = params.value("tool_arguments", nlohmann::json::object());
            return actionCallTool(url, sessionId, authToken, toolName, toolArgs, timeout);
        }
        if (action == "list_resources")
            return actionListResources(url, sessionId, authToken, timeout);
        if (action == "read_resource")
        {
            auto resourceUri = params.at("resource_uri").get<std::string>();
            return actionReadResource(url, sessionId, authToken, resourceUri, timeout);
        }
        if (action == "ping")
            return actionPing(url, sessionId, authToken, timeout);
        if (action == "close")
            return actionClose(url, sessionId, authToken, timeout);

        return "Error: Unknown action '" + action + "'. Valid actions: initialize, list_tools, call_tool, list_resources, read_resource, ping, close";
    }
    catch (const nlohmann::json::exception &e)
    {
        return "Error: Invalid JSON in MCP response: " + std::string(e.what());
    }
    catch (const std::exception &e)
    {
        return "Error: " + std::string(e.what());
    }
}

ToolResult McpClientTool::actionInitialize(const std::string &url, const std::string &authToken, int timeout)
{
    auto request = nlohmann::json{
        {"jsonrpc", "2.0"},
        {"id", nextRequestId()},
        {"method", "initialize"},
        {"params", {{"protocolVersion", "2025-03-26"}, {"capabilities", nlohmann::json::object()}, {"clientInfo", {{"name", "IonClaw"}, {"version", IONCLAW_VERSION_STRING}}}}}};

    auto response = sendRpcRequest(url, request, "", authToken, timeout);

    auto sessionId = response.value("_mcp_session_id", std::string(""));
    auto result = response.value("result", nlohmann::json::object());

    // send notifications/initialized
    auto notif = nlohmann::json{
        {"jsonrpc", "2.0"},
        {"method", "notifications/initialized"}};

    // notifications/initialized may return 202 with no body — ignore response
    try
    {
        std::map<std::string, std::string> headers;
        headers["Content-Type"] = "application/json";
        headers["Accept"] = "application/json, text/event-stream";

        if (!sessionId.empty())
        {
            headers["MCP-Session-Id"] = sessionId;
        }

        if (!authToken.empty())
        {
            headers["Authorization"] = "Bearer " + authToken;
        }

        ionclaw::util::HttpClient::request("POST", url, headers, notif.dump(), timeout, false);
    }
    catch (const std::exception &e)
    {
        spdlog::warn("[McpClient] notifications/initialized failed (non-fatal): {}", e.what());
    }

    auto output = nlohmann::json{
        {"session_id", sessionId},
        {"protocol_version", result.value("protocolVersion", std::string(""))},
        {"server_info", result.value("serverInfo", nlohmann::json::object())},
        {"capabilities", result.value("capabilities", nlohmann::json::object())}};

    if (sessionId.empty())
    {
        spdlog::info("[McpClient] Initialized with {} (no session ID)", url);
    }
    else
    {
        spdlog::info("[McpClient] Initialized session {} with {}", sessionId, url);
    }

    return output.dump(2);
}

ToolResult McpClientTool::actionListTools(const std::string &url, const std::string &sessionId, const std::string &authToken, int timeout)
{
    auto request = nlohmann::json{
        {"jsonrpc", "2.0"},
        {"id", nextRequestId()},
        {"method", "tools/list"}};

    auto response = sendRpcRequest(url, request, sessionId, authToken, timeout);
    auto result = response.value("result", nlohmann::json::object());

    return result.dump(2);
}

ToolResult McpClientTool::actionCallTool(
    const std::string &url,
    const std::string &sessionId,
    const std::string &authToken,
    const std::string &toolName,
    const nlohmann::json &toolArgs,
    int timeout)
{
    auto request = nlohmann::json{
        {"jsonrpc", "2.0"},
        {"id", nextRequestId()},
        {"method", "tools/call"},
        {"params", {{"name", toolName}, {"arguments", toolArgs}}}};

    auto response = sendRpcRequest(url, request, sessionId, authToken, timeout);
    auto result = response.value("result", nlohmann::json::object());

    // extract text content from MCP tool result
    if (result.contains("content") && result["content"].is_array())
    {
        std::string text;

        for (const auto &block : result["content"])
        {
            if (block.value("type", "") == "text")
            {
                if (!text.empty())
                {
                    text += "\n";
                }
                text += block.value("text", "");
            }
        }

        auto isError = result.value("isError", false);
        auto output = nlohmann::json{
            {"tool", toolName},
            {"text", text},
            {"isError", isError}};

        return output.dump(2);
    }

    return result.dump(2);
}

ToolResult McpClientTool::actionListResources(const std::string &url, const std::string &sessionId, const std::string &authToken, int timeout)
{
    auto request = nlohmann::json{
        {"jsonrpc", "2.0"},
        {"id", nextRequestId()},
        {"method", "resources/list"}};

    auto response = sendRpcRequest(url, request, sessionId, authToken, timeout);
    auto result = response.value("result", nlohmann::json::object());

    return result.dump(2);
}

ToolResult McpClientTool::actionReadResource(
    const std::string &url,
    const std::string &sessionId,
    const std::string &authToken,
    const std::string &resourceUri,
    int timeout)
{
    auto request = nlohmann::json{
        {"jsonrpc", "2.0"},
        {"id", nextRequestId()},
        {"method", "resources/read"},
        {"params", {{"uri", resourceUri}}}};

    auto response = sendRpcRequest(url, request, sessionId, authToken, timeout);
    auto result = response.value("result", nlohmann::json::object());

    // extract text from resource contents
    if (result.contains("contents") && result["contents"].is_array() && !result["contents"].empty())
    {
        auto &first = result["contents"][0];
        auto text = first.value("text", std::string(""));

        if (!text.empty())
        {
            auto output = nlohmann::json{
                {"uri", first.value("uri", resourceUri)},
                {"mimeType", first.value("mimeType", "")},
                {"text", text}};

            return output.dump(2);
        }
    }

    return result.dump(2);
}

ToolResult McpClientTool::actionPing(const std::string &url, const std::string &sessionId, const std::string &authToken, int timeout)
{
    auto request = nlohmann::json{
        {"jsonrpc", "2.0"},
        {"id", nextRequestId()},
        {"method", "ping"}};

    sendRpcRequest(url, request, sessionId, authToken, timeout);

    return R"({"status": "ok"})";
}

ToolResult McpClientTool::actionClose(const std::string &url, const std::string &sessionId, const std::string &authToken, int timeout)
{
    sendDeleteRequest(url, sessionId, authToken, timeout);

    if (sessionId.empty())
    {
        spdlog::info("[McpClient] Closed connection to {}", url);
    }
    else
    {
        spdlog::info("[McpClient] Closed session {} with {}", sessionId, url);
    }

    return R"({"status": "closed"})";
}

ToolSchema McpClientTool::schema() const
{
    return {
        "mcp_client",
        "Connect to an external MCP server and interact with its tools and resources. "
        "Use initialize to start a session, then list_tools/call_tool to use remote tools, "
        "list_resources/read_resource to read remote data, and close to end the session.",
        {{"type", "object"},
         {"properties",
          {{"action", {{"type", "string"}, {"enum", nlohmann::json::array({"initialize", "list_tools", "call_tool", "list_resources", "read_resource", "ping", "close"})}, {"description", "Action to perform"}}},
           {"url", {{"type", "string"}, {"description", "MCP server endpoint URL (e.g. http://localhost:8080/mcp)"}}},
           {"session_id", {{"type", "string"}, {"description", "MCP session ID returned by initialize (pass it if the server provides one)"}}},
           {"auth_token", {{"type", "string"}, {"description", "Bearer token for authenticated MCP servers"}}},
           {"tool_name", {{"type", "string"}, {"description", "Tool name to call (required for call_tool)"}}},
           {"tool_arguments", {{"type", "object"}, {"description", "Arguments for the tool call (call_tool)"}}},
           {"resource_uri", {{"type", "string"}, {"description", "Resource URI to read (required for read_resource)"}}},
           {"timeout", {{"type", "integer"}, {"description", "Request timeout in seconds (default: 30)"}}}}},
         {"required", nlohmann::json::array({"action", "url"})}}};
}

} // namespace builtin
} // namespace tool
} // namespace ionclaw
