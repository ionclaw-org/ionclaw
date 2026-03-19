#include "ionclaw/tool/builtin/MessageTool.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

ToolResult MessageTool::execute(const nlohmann::json &params, const ToolContext &context)
{
    auto content = params.at("content").get<std::string>();

    if (!context.messageSender)
    {
        return "Error: message sender is not configured";
    }

    // resolve channel and chat_id from params or context
    std::string channel = context.agentName;
    std::string chatId = context.sessionKey;

    if (params.contains("channel") && params["channel"].is_string())
    {
        channel = params["channel"].get<std::string>();
    }

    if (params.contains("chat_id") && params["chat_id"].is_string())
    {
        chatId = params["chat_id"].get<std::string>();
    }

    context.messageSender(channel, chatId, content);

    return "Message sent to " + channel + ":" + chatId;
}

ToolSchema MessageTool::schema() const
{
    return {
        "message",
        "Send a message to the user. Use this to communicate progress, ask questions, or deliver results.",
        {{"type", "object"},
         {"properties",
          {{"content", {{"type", "string"}, {"description", "The message content to send to the user"}}},
           {"channel", {{"type", "string"}, {"description", "Target channel (defaults to current channel)"}}},
           {"chat_id", {{"type", "string"}, {"description", "Target chat ID (defaults to current session)"}}}}},
         {"required", nlohmann::json::array({"content"})}}};
}

} // namespace builtin
} // namespace tool
} // namespace ionclaw
