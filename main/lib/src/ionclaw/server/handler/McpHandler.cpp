#include "ionclaw/server/handler/McpHandler.hpp"

#include <chrono>
#include <iterator>
#include <thread>

#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace server
{
namespace handler
{

McpHandler::McpHandler(
    std::shared_ptr<Auth> auth,
    std::shared_ptr<ionclaw::mcp::McpDispatcher> mcpDispatcher)
    : auth(std::move(auth))
    , mcpDispatcher(std::move(mcpDispatcher))
{
}

void McpHandler::handleRequest(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp)
{
    // CORS headers for browser-based MCP clients
    resp.set("Access-Control-Allow-Origin", "*");
    resp.set("Access-Control-Allow-Headers", "Authorization, Content-Type, MCP-Session-Id, MCP-Protocol-Version");
    resp.set("Access-Control-Expose-Headers", "MCP-Session-Id");

    if (req.getMethod() == "OPTIONS")
    {
        resp.set("Access-Control-Allow-Methods", "POST, GET, DELETE, OPTIONS");
        resp.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_NO_CONTENT);
        resp.send();
        return;
    }

    // MCP spec 2025-11-25: validate Origin header to prevent DNS rebinding attacks
    auto origin = req.get("Origin", "");
    if (!origin.empty() && !mcpDispatcher->isAllowedOrigin(origin))
    {
        resp.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_FORBIDDEN);
        resp.setContentType("application/json");
        auto &ostr = resp.send();
        ostr << R"({"jsonrpc":"2.0","id":null,"error":{"code":-32600,"message":"Forbidden: invalid Origin"}})";
        return;
    }

    if (!mcpDispatcher->isEnabled())
    {
        resp.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_SERVICE_UNAVAILABLE);
        resp.setContentType("application/json");
        auto &ostr = resp.send();
        ostr << R"({"error":"MCP channel is disabled"})";
        return;
    }

    if (req.getMethod() == "POST")
        handlePost(req, resp);
    else if (req.getMethod() == "GET")
        handleGet(req, resp);
    else if (req.getMethod() == "DELETE")
        handleDelete(req, resp);
    else
    {
        resp.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_METHOD_NOT_ALLOWED);
        resp.send();
    }
}

bool McpHandler::checkAuth(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp)
{
    if (!mcpDispatcher->requiresAuth())
    {
        return true;
    }
    auto authHeader = req.get("Authorization", "");
    auto token = Auth::extractBearerToken(authHeader);
    if (!mcpDispatcher->verifyToken(token))
    {
        resp.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_UNAUTHORIZED);
        resp.setContentType("application/json");
        auto &ostr = resp.send();
        ostr << R"({"error":"Unauthorized"})";
        return false;
    }
    return true;
}

void McpHandler::handlePost(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp)
{
    if (!checkAuth(req, resp))
    {
        return;
    }

    // read and parse request body
    nlohmann::json body;
    try
    {
        std::string bodyStr(
            std::istreambuf_iterator<char>(req.stream()),
            std::istreambuf_iterator<char>());
        body = nlohmann::json::parse(bodyStr);
    }
    catch (...)
    {
        resp.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST);
        resp.setContentType("application/json");
        auto &ostr = resp.send();
        ostr << R"({"jsonrpc":"2.0","id":null,"error":{"code":-32700,"message":"Parse error"}})";
        return;
    }

    // batch requests are not supported
    if (body.is_array())
    {
        resp.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST);
        resp.setContentType("application/json");
        auto &ostr = resp.send();
        ostr << R"({"jsonrpc":"2.0","id":null,"error":{"code":-32600,"message":"Batch requests not supported"}})";
        return;
    }

    bool isInitialize = body.value("method", "") == "initialize";

    // get or create MCP session
    auto sessionId = req.get("MCP-Session-Id", "");

    if (sessionId.empty())
    {
        if (!isInitialize)
        {
            resp.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST);
            resp.setContentType("application/json");
            auto &ostr = resp.send();
            ostr << R"({"jsonrpc":"2.0","id":null,"error":{"code":-32600,"message":"MCP-Session-Id header required"}})";
            return;
        }
        sessionId = mcpDispatcher->createSession();
    }
    else if (!mcpDispatcher->hasSession(sessionId))
    {
        resp.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_NOT_FOUND);
        resp.setContentType("application/json");
        auto &ostr = resp.send();
        ostr << R"({"jsonrpc":"2.0","id":null,"error":{"code":-32600,"message":"Session not found"}})";
        return;
    }

    resp.set("MCP-Session-Id", sessionId);

    // parse the JSON-RPC request
    ionclaw::mcp::JsonRpcRequest request;
    try
    {
        request = ionclaw::mcp::JsonRpcRequest::parse(body);
    }
    catch (...)
    {
        resp.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST);
        resp.setContentType("application/json");
        auto &ostr = resp.send();
        ostr << R"({"jsonrpc":"2.0","id":null,"error":{"code":-32700,"message":"Parse error"}})";
        return;
    }

    bool wantsSSE = req.get("Accept", "").find("text/event-stream") != std::string::npos;
    bool isToolCall = request.method == "tools/call";

    if (wantsSSE && isToolCall)
    {
        // streaming response via SSE (one-shot: close connection after response)
        resp.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_OK);
        resp.setContentType("text/event-stream");
        resp.set("Cache-Control", "no-cache");
        resp.setChunkedTransferEncoding(true);

        auto &ostr = resp.send();

        // MCP spec 2025-11-25: prime the client with an event ID for reconnection
        ostr << "id: " << sessionId << ":0\ndata: \n\n";
        ostr.flush();

        int eventSeq = 1;
        ionclaw::mcp::SseCallback callback = [&ostr, &sessionId, &eventSeq](const nlohmann::json &event) -> bool
        {
            ostr << "id: " << sessionId << ":" << eventSeq++ << "\n";
            ostr << "event: message\ndata: " << event.dump() << "\n\n";
            ostr.flush();
            return ostr.good();
        };

        try
        {
            auto result = mcpDispatcher->dispatch(sessionId, request, &callback);

            // send the final JSON-RPC response as the closing SSE event
            if (!result.is_null())
            {
                ostr << "id: " << sessionId << ":" << eventSeq++ << "\n";
                ostr << "event: message\ndata: " << result.dump() << "\n\n";
                ostr.flush();
            }
        }
        catch (const std::exception &e)
        {
            spdlog::error("[MCP] SSE dispatch exception: {}", e.what());
            auto errResult = ionclaw::mcp::JsonRpcResponse::err(request.id, -32603, e.what());
            ostr << "id: " << sessionId << ":" << eventSeq++ << "\n";
            ostr << "event: message\ndata: " << errResult.dump() << "\n\n";
            ostr.flush();
        }
    }
    else
    {
        auto result = mcpDispatcher->dispatch(sessionId, request, nullptr);

        // notifications have no id and produce no response body — spec: 202 Accepted
        if (result.is_null())
        {
            resp.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_ACCEPTED);
            resp.send();
            return;
        }

        resp.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_OK);
        resp.setContentType("application/json");
        auto &ostr = resp.send();
        ostr << result.dump();
    }
}

void McpHandler::handleGet(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp)
{
    if (!checkAuth(req, resp))
    {
        return;
    }

    auto sessionId = req.get("MCP-Session-Id", "");
    if (sessionId.empty())
    {
        resp.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST);
        resp.setContentType("application/json");
        auto &ostr = resp.send();
        ostr << R"({"error":"MCP-Session-Id header required"})";
        return;
    }
    if (!mcpDispatcher->hasSession(sessionId))
    {
        resp.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_NOT_FOUND);
        resp.setContentType("application/json");
        auto &ostr = resp.send();
        ostr << R"({"error":"Session not found"})";
        return;
    }

    resp.set("MCP-Session-Id", sessionId);
    resp.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_OK);
    resp.setContentType("text/event-stream");
    resp.set("Cache-Control", "no-cache");
    resp.set("Connection", "keep-alive");
    resp.setChunkedTransferEncoding(true);

    auto &ostr = resp.send();

    // keep the SSE connection alive with periodic pings
    while (ostr.good() && mcpDispatcher->hasSession(sessionId))
    {
        auto ping = ionclaw::mcp::JsonRpcResponse::notification("ping", nlohmann::json::object());
        ostr << "event: message\ndata: " << ping.dump() << "\n\n";
        ostr.flush();

        if (!ostr.good())
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::seconds(30));
    }
}

void McpHandler::handleDelete(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp)
{
    if (!checkAuth(req, resp))
    {
        return;
    }

    auto sessionId = req.get("MCP-Session-Id", "");
    if (!sessionId.empty())
    {
        mcpDispatcher->closeSession(sessionId);
    }

    resp.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_OK);
    resp.send();
}

} // namespace handler
} // namespace server
} // namespace ionclaw
