#pragma once

#include <chrono>
#include <string>

#include "nlohmann/json.hpp"

namespace ionclaw
{
namespace mcp
{

static constexpr const char *MCP_PROTOCOL_VERSION = "2025-03-26";
static constexpr const char *MCP_CHANNEL = "mcp";

enum class RpcErrorCode : int
{
    ParseError = -32700,
    InvalidRequest = -32600,
    MethodNotFound = -32601,
    InvalidParams = -32602,
    Internal = -32603
};

struct JsonRpcRequest
{
    std::string jsonrpc;
    nlohmann::json id = nullptr;
    std::string method;
    nlohmann::json params = nlohmann::json::object();

    bool isNotification() const { return id.is_null(); }

    static JsonRpcRequest parse(const nlohmann::json &j)
    {
        JsonRpcRequest req;
        req.jsonrpc = j.value("jsonrpc", "");
        req.method = j.value("method", "");
        if (j.contains("id"))
        {
            req.id = j["id"];
        }
        if (j.contains("params") && j["params"].is_object())
        {
            req.params = j["params"];
        }
        return req;
    }
};

struct JsonRpcResponse
{
    static nlohmann::json ok(const nlohmann::json &id, const nlohmann::json &result)
    {
        return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
    }

    static nlohmann::json err(const nlohmann::json &id, int code, const std::string &message)
    {
        return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
    }

    static nlohmann::json notification(const std::string &method, const nlohmann::json &params)
    {
        return {{"jsonrpc", "2.0"}, {"method", method}, {"params", params}};
    }
};

struct McpSession
{
    std::string id;
    std::string chatId;
    bool initialized = false;
    std::string protocolVersion;
    std::chrono::system_clock::time_point createdAt = std::chrono::system_clock::now();
};

} // namespace mcp
} // namespace ionclaw
