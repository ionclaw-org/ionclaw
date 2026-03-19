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
    // mcp spec 2025-11-25: validate origin header to prevent dns rebinding attacks
    auto origin = req.get("Origin", "");
    if (!origin.empty() && !mcpDispatcher->isAllowedOrigin(origin))
    {
        resp.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_FORBIDDEN);
        resp.setContentType("application/json");
        auto &ostr = resp.send();
        ostr << R"({"jsonrpc":"2.0","id":null,"error":{"code":-32600,"message":"Forbidden: invalid Origin"}})";
        return;
    }

    // cors headers set after origin validation to avoid leaking access on rejected origins
    auto allowOrigin = origin.empty() ? std::string("*") : origin;
    resp.set("Access-Control-Allow-Origin", allowOrigin);
    resp.set("Access-Control-Allow-Headers", "Authorization, Content-Type, MCP-Session-Id, MCP-Protocol-Version");
    resp.set("Access-Control-Expose-Headers", "MCP-Session-Id");

    if (req.getMethod() == "OPTIONS")
    {
        resp.set("Access-Control-Allow-Methods", "POST, GET, DELETE, OPTIONS");
        resp.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_NO_CONTENT);
        resp.send();
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

    // read and parse request body (capped at 10MB to prevent OOM)
    static constexpr size_t MAX_BODY_SIZE = 10 * 1024 * 1024;
    nlohmann::json body;
    try
    {
        std::string bodyStr;
        auto &istr = req.stream();
        char buf[8192];

        while (istr.read(buf, sizeof(buf)) || istr.gcount() > 0)
        {
            bodyStr.append(buf, static_cast<size_t>(istr.gcount()));

            if (bodyStr.size() > MAX_BODY_SIZE)
            {
                resp.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST);
                resp.setContentType("application/json");
                auto &ostr = resp.send();
                ostr << R"({"jsonrpc":"2.0","id":null,"error":{"code":-32600,"message":"Request body too large"}})";
                return;
            }
        }

        body = nlohmann::json::parse(bodyStr);
    }
    catch (const nlohmann::json::parse_error &)
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
    catch (const nlohmann::json::parse_error &)
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

        // mcp spec 2025-11-25: prime the client with an event id for reconnection
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
            auto errResult = ionclaw::mcp::JsonRpcResponse::err(request.id, -32603, "internal error");
            ostr << "id: " << sessionId << ":" << eventSeq++ << "\n";
            ostr << "event: message\ndata: " << errResult.dump() << "\n\n";
            ostr.flush();
        }
    }
    else
    {
        try
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
        catch (const std::exception &e)
        {
            spdlog::error("[MCP] dispatch exception: {}", e.what());
            resp.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_OK);
            resp.setContentType("application/json");
            auto &ostr = resp.send();
            auto errResult = ionclaw::mcp::JsonRpcResponse::err(request.id, -32603, "internal error");
            ostr << errResult.dump();
        }
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
    // use shorter sleep intervals to release threads faster on disconnect
    static constexpr int PING_INTERVAL_SECONDS = 30;
    static constexpr int MAX_SSE_DURATION_SECONDS = 3600;
    auto sseStart = std::chrono::steady_clock::now();

    while (ostr.good() && mcpDispatcher->hasSession(sessionId))
    {
        // check maximum SSE connection duration to prevent indefinite thread hold
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::steady_clock::now() - sseStart)
                           .count();

        if (elapsed >= MAX_SSE_DURATION_SECONDS)
        {
            break;
        }

        auto ping = ionclaw::mcp::JsonRpcResponse::notification("ping", nlohmann::json::object());
        ostr << "event: message\ndata: " << ping.dump() << "\n\n";
        ostr.flush();

        if (!ostr.good())
        {
            break;
        }

        // sleep in shorter intervals to detect session removal faster
        for (int i = 0; i < PING_INTERVAL_SECONDS && mcpDispatcher->hasSession(sessionId); ++i)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
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
