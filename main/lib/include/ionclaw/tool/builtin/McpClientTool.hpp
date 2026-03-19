#pragma once

#include <cstdint>
#include <string>

#include "ionclaw/tool/Tool.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

class McpClientTool final : public Tool
{
public:
    ToolResult execute(const nlohmann::json &params, const ToolContext &context) override;
    ToolSchema schema() const override;

private:
    static constexpr int DEFAULT_TIMEOUT_SECONDS = 30;

    // action handlers
    ToolResult actionInitialize(const std::string &url, const std::string &authToken, int timeout);
    ToolResult actionListTools(const std::string &url, const std::string &sessionId, const std::string &authToken, int timeout);
    ToolResult actionCallTool(const std::string &url, const std::string &sessionId, const std::string &authToken, const std::string &toolName, const nlohmann::json &toolArgs, int timeout);
    ToolResult actionListResources(const std::string &url, const std::string &sessionId, const std::string &authToken, int timeout);
    ToolResult actionReadResource(const std::string &url, const std::string &sessionId, const std::string &authToken, const std::string &resourceUri, int timeout);
    ToolResult actionPing(const std::string &url, const std::string &sessionId, const std::string &authToken, int timeout);
    ToolResult actionClose(const std::string &url, const std::string &sessionId, const std::string &authToken, int timeout);

    // helpers
    static nlohmann::json sendRpcRequest(const std::string &url, const nlohmann::json &request, const std::string &sessionId, const std::string &authToken, int timeout);
    static void sendDeleteRequest(const std::string &url, const std::string &sessionId, const std::string &authToken, int timeout);
    static int64_t nextRequestId();
};

} // namespace builtin
} // namespace tool
} // namespace ionclaw
