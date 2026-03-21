#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ionclaw/agent/MemoryStore.hpp"
#include "ionclaw/agent/SkillsLoader.hpp"
#include "ionclaw/config/Config.hpp"
#include "ionclaw/provider/LlmProvider.hpp"
#include "ionclaw/session/SessionManager.hpp"

namespace ionclaw
{
namespace agent
{

enum class PromptMode
{
    Full,
    Minimal
};

class ContextBuilder
{
public:
    ContextBuilder(
        const ionclaw::config::Config &config,
        const std::string &workspacePath,
        std::shared_ptr<MemoryStore> memory,
        std::shared_ptr<SkillsLoader> skillsLoader);

    std::string buildSystemPrompt(
        const std::string &agentName,
        const std::string &agentInstructions = "",
        const std::string &channel = "web",
        const std::vector<std::string> &toolNames = {},
        const std::map<std::string, std::string> &toolDescriptions = {},
        PromptMode mode = PromptMode::Full,
        const std::string &userLanguage = "") const;

    static std::vector<ionclaw::provider::Message> buildMessages(
        const std::string &systemPrompt,
        const std::vector<ionclaw::session::SessionMessage> &history,
        const std::string &userContent,
        const nlohmann::json &mediaBlocks = nlohmann::json::array(),
        const std::map<int, nlohmann::json> &historyMediaBlocks = {});

    static void addToolResult(
        std::vector<ionclaw::provider::Message> &messages,
        const std::string &toolCallId,
        const std::string &toolName,
        const std::string &result,
        const nlohmann::json &media = nlohmann::json());

    static void addAssistantMessage(
        std::vector<ionclaw::provider::Message> &messages,
        const std::string &content,
        const std::vector<ionclaw::provider::ToolCall> &toolCalls = {},
        const std::string &reasoningContent = "");

    static std::string buildSubagentContext(int depth, int maxDepth);

    // cap oversized tool results in message history to fit within token budget
    static void enforceToolResultBudget(
        std::vector<ionclaw::provider::Message> &messages,
        int maxTotalChars);

    // strip thinking/reasoning content from history messages (keep only last)
    static void stripThinkingFromHistory(std::vector<ionclaw::provider::Message> &messages);

    // replace image content blocks with text markers in older messages
    static void pruneHistoryImages(std::vector<ionclaw::provider::Message> &messages, int keepRecent = 4);

    // build annotation text for media paths (e.g. "[image attached: path — use vision tool...]")
    static std::string buildMediaAnnotation(const std::vector<nlohmann::json> &media);

    // repair tool use / result pairing after history trimming
    static void repairToolUseResultPairing(std::vector<ionclaw::provider::Message> &messages);

private:
    const ionclaw::config::Config &config;
    std::string workspacePath;
    std::shared_ptr<MemoryStore> memory;
    std::shared_ptr<SkillsLoader> skillsLoader;

    // prompt constants
    static const char *IDENTITY_PREFIX;
    static const char *RESPONSE_GUIDELINES;
    static const std::vector<std::string> BOOTSTRAP_FILES;
    static constexpr size_t BOOTSTRAP_MAX_CHARS = 20000;
    static constexpr size_t BOOTSTRAP_TOTAL_MAX_CHARS = 80000;

    // helpers
    static std::string getChannelGuidance(const std::string &channel);
    static std::string contentToText(const nlohmann::json &content);

    std::string readProjectFile(const std::string &filename) const;
    std::string buildDirectoryContext() const;
};

} // namespace agent
} // namespace ionclaw
