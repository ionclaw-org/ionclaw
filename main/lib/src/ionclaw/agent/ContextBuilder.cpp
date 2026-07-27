#include "ionclaw/agent/ContextBuilder.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "ionclaw/util/PipeGuard.hpp"

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#include "ionclaw/tool/Platform.hpp"
#include "ionclaw/util/StringHelper.hpp"
#include "ionclaw/util/TimeHelper.hpp"
#include "spdlog/spdlog.h"

namespace fs = std::filesystem;

namespace ionclaw
{
namespace agent
{

// prompt constants
const char *ContextBuilder::IDENTITY_PREFIX = "You are ";
const char *ContextBuilder::RESPONSE_GUIDELINES =
    "- Be concise and actionable.\n"
    "- When using tools, briefly explain what you are doing.\n"
    "- If a task requires multiple steps, outline your plan before executing.\n"
    "- When an error occurs, analyze the cause and try a different approach.\n"
    "- Use Markdown formatting for structured responses.";

const std::vector<std::string> ContextBuilder::BOOTSTRAP_FILES = {"AGENTS.md", "SOUL.md", "USER.md", "TOOLS.md"};

std::string ContextBuilder::getChannelGuidance(const std::string &channel)
{
    if (channel == "telegram")
    {
        return "Channel: Telegram. Keep messages under 4096 characters. "
               "Use Markdown (bold, italic, code). Split long responses using the message tool.";
    }

    if (channel == "heartbeat")
    {
        return "Channel: heartbeat. This is a periodic check-in.\n"
               "Read HEARTBEAT.md in your workspace (if it exists) and follow any instructions there.\n"
               "If nothing needs attention, reply with just: HEARTBEAT_OK";
    }

    if (channel == "cron")
    {
        return "Channel: cron. This is a scheduled task execution.\n"
               "Focus on completing the requested task efficiently. Be concise in your response.";
    }

    return "Channel: web. Use Markdown formatting freely including images, code blocks, and tables.";
}

std::string ContextBuilder::contentToText(const nlohmann::json &content)
{
    if (content.is_string())
    {
        return content.get<std::string>();
    }

    if (content.is_array())
    {
        // concatenate text blocks with double newlines
        std::ostringstream parts;
        bool first = true;

        for (const auto &block : content)
        {
            if (block.is_object() && block.contains("type") && block["type"].is_string() && block["type"].get<std::string>() == "text")
            {
                auto text = block.contains("text") && block["text"].is_string() ? block["text"].get<std::string>() : "";

                if (!text.empty())
                {
                    if (!first)
                    {
                        parts << "\n\n";
                    }

                    parts << text;
                    first = false;
                }
            }
        }

        return parts.str();
    }

    return "";
}

ContextBuilder::ContextBuilder(const ionclaw::config::Config &config, const std::string &workspacePath, std::shared_ptr<MemoryStore> memory, std::shared_ptr<SkillsLoader> skillsLoader) : config(config) , workspacePath(workspacePath) , memory(std::move(memory)) , skillsLoader(std::move(skillsLoader))
{
}

std::string ContextBuilder::buildDirectoryContext() const
{
    std::ostringstream lines;

    lines << "You work inside your workspace. Use paths relative to it (e.g. public/media/image.png).\n\n";

    // agent workspace (sanitize paths for prompt safety)
    auto safePath = ionclaw::util::StringHelper::sanitizeForPrompt(workspacePath);
    lines << "Workspace: " << safePath << "\n";
    lines << "  Internal files: reports, data, drafts.\n";
    lines << "  - " << safePath << "/skills"
          << " - your skills (each is a directory with a SKILL.md, override project-level skills)\n";
    lines << "  - " << safePath << "/public"
          << " - files published on the web (see the public delivery rules in the system prompt)\n";
    lines << "    Organize by type and date (YYYY/MM/DD) to avoid cluttering a single folder:\n";
    lines << "    - " << safePath << "/public/media" << " - images, audio, video\n";
    lines << "    - " << safePath << "/public/documents" << " - PDFs, spreadsheets, exports\n";
    lines << "    - " << safePath << "/public/sites" << " - HTML pages, static sites\n";

    return lines.str();
}

std::string ContextBuilder::buildSystemPrompt(const std::string &agentName, const std::string &agentInstructions, const std::string &channel, const std::vector<std::string> &toolNames, const std::map<std::string, std::string> &toolDescriptions, PromptMode mode, const std::string &userLanguage) const
{
    std::ostringstream prompt;
    bool full = (mode == PromptMode::Full);

    // 1. identity (sanitize runtime strings for prompt safety)
    prompt << IDENTITY_PREFIX << ionclaw::util::StringHelper::sanitizeForPrompt(config.bot.name)
           << ", a personal AI assistant.";

    // 2. agent name
    if (!agentName.empty())
    {
        prompt << "\nYou are acting as the **"
               << ionclaw::util::StringHelper::sanitizeForPrompt(agentName) << "** agent.";
    }

    // 3. safety guidelines (full mode only)
    if (full)
    {
        prompt << "\n\n# Safety\n"
               << "- You have no independent goals beyond assisting the user.\n"
               << "- Prioritize safety and do not take actions that could cause harm.\n"
               << "- Do not attempt to manipulate, deceive, or bypass safeguards.\n"
               << "- If a request seems harmful, explain your concern rather than complying.\n"
               << "- Follow applicable laws and respect intellectual property.";
    }

    // 4. current date/time and user language
    prompt << "\n\nCurrent date and time: " << ionclaw::util::TimeHelper::nowInZone(config.timezone);

    if (!config.timezone.empty())
    {
        prompt << " (" << ionclaw::util::StringHelper::sanitizeForPrompt(config.timezone) << ")";
    }

    prompt << ".";

    if (!userLanguage.empty())
    {
        prompt << "\nUser language: " << ionclaw::util::StringHelper::sanitizeForPrompt(userLanguage) << ".";
    }

    // 5. runtime info
    prompt << "\n\n# Runtime\n";
    prompt << "Platform: " << ionclaw::tool::Platform::current();

    // detect OS version (desktop only, popen unavailable on iOS/Android)
    {
        std::string version;

#if defined(__APPLE__) && !(TARGET_OS_IOS || TARGET_OS_SIMULATOR)
        ionclaw::util::PipeGuard pipe("sw_vers -productVersion 2>/dev/null");
#elif defined(__linux__) && !defined(__ANDROID__)
        ionclaw::util::PipeGuard pipe("uname -r 2>/dev/null");
#elif defined(_WIN32)
        ionclaw::util::PipeGuard pipe("ver 2>nul");
#else
        ionclaw::util::PipeGuard pipe(nullptr);
#endif

        if (pipe)
        {
            std::array<char, 128> buffer;

            while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()))
            {
                version += buffer.data();
            }

            while (!version.empty() && (version.back() == '\n' || version.back() == '\r' || version.back() == ' '))
            {
                version.pop_back();
            }

            if (!version.empty())
            {
                prompt << " " << version;
            }
        }
    }

    // 6. channel guidance
    prompt << "\n\n"
           << getChannelGuidance(channel);

    // 7. public files and delivery
    std::string baseUrl = config.server.publicUrl;

    if (baseUrl.empty())
    {
        baseUrl = "http://localhost:" + std::to_string(config.server.port);
    }

    if (baseUrl.back() == '/')
    {
        baseUrl.pop_back();
    }

    // public files are served under the /public path, so the public url is the base url plus /public
    auto publicUrl = ionclaw::util::StringHelper::sanitizeForPrompt(baseUrl + "/public");
    auto safeChannel = ionclaw::util::StringHelper::sanitizeForPrompt(channel);

    prompt << "\n\n# Public Files and Delivery\n"
           << "- You are responding on the \"" << safeChannel << "\" channel.\n"
           << "- Files you save under public/ in your workspace are published on the web: a file's url is " << publicUrl << " plus its path below public/. For example, saving public/media/chart.png publishes it at " << publicUrl << "/media/chart.png.\n"
           << "- When you create something the user should open or download (an image, a report, a QR code), save it under public/.\n";

    // whatsapp and telegram deliver real attachments via inline markers, other non-terminal channels only carry the public url
    if (channel == "whatsapp" || channel == "telegram")
    {
        prompt << "- To attach a file to your reply, put a marker where you want it sent: [[image:public/media/chart.png]], [[audio:public/media/reply.ogg]], [[video:public/media/clip.mp4]] or [[document:public/reports/report.pdf]]. Use [[media:PATH]] to let the file extension pick the type. The text around the marker becomes the caption.\n"
               << "- Use [[break]] to split your reply into separate messages.\n"
               << "- Prefer sending a generated file as an attachment with a marker over pasting its raw url.\n";
    }
    else
    {
        prompt << "- On channels other than the terminal you cannot attach files, so this url is the only way to deliver a file you generated.\n"
               << "- Whenever you save a file under public/ during this turn, your reply to the user MUST include its full public url.\n";
    }

    prompt << "- Never publish secrets, personal data, or private files.";

    // 8. directory context
    prompt << "\n\n# Project Structure\n"
           << buildDirectoryContext();

    // 9. response guidelines (full mode only)
    if (full)
    {
        prompt << "\n# Guidelines\n"
               << RESPONSE_GUIDELINES;
    }

    // 10. available tools with descriptions (already filtered by platform and agent config)
    if (!toolNames.empty())
    {
        prompt << "\n\n# Available Tools\n"
               << "These tools are available for the current platform. Only use tools listed here.\n";

        for (const auto &name : toolNames)
        {
            auto it = toolDescriptions.find(name);

            if (it != toolDescriptions.end() && !it->second.empty())
            {
                prompt << "- " << name << ": " << it->second << "\n";
            }
            else
            {
                prompt << "- " << name << "\n";
            }
        }
    }

    // lightweight channels skip bootstrap, memory, and skills to save tokens
    bool lightweight = (channel == "heartbeat" || channel == "cron");

    // 11. bootstrap files (full mode only, skip for lightweight channels)
    if (full && !lightweight)
    {
        try
        {
            size_t totalBootstrapChars = 0;

            for (const auto &filename : BOOTSTRAP_FILES)
            {
                if (totalBootstrapChars >= BOOTSTRAP_TOTAL_MAX_CHARS)
                {
                    break;
                }

                auto content = readProjectFile(filename);

                if (content.empty())
                {
                    continue;
                }

                // trim whitespace
                auto start = content.find_first_not_of(" \t\n\r");
                auto end = content.find_last_not_of(" \t\n\r");

                if (start != std::string::npos)
                {
                    content = content.substr(start, end - start + 1);
                }

                // truncate oversized files: keep 70% head + 20% tail (utf-8 safe)
                if (content.size() > BOOTSTRAP_MAX_CHARS)
                {
                    auto headSize = BOOTSTRAP_MAX_CHARS * 7 / 10;
                    auto tailSize = BOOTSTRAP_MAX_CHARS * 2 / 10;
                    auto head = ionclaw::util::StringHelper::utf8SafeTruncate(content, headSize);
                    auto safeTailSize = std::min(tailSize, content.size());
                    auto tailStart = content.size() - safeTailSize;

                    // advance past any utf-8 continuation bytes to avoid splitting a multi-byte sequence
                    while (tailStart < content.size() && (static_cast<unsigned char>(content[tailStart]) & 0xC0) == 0x80)
                    {
                        tailStart++;
                    }

                    auto tail = content.substr(tailStart);
                    content = head + "\n[...truncated, read " + filename + " for full content...]\n" + tail;
                }

                // enforce total budget (utf-8 safe)
                auto remaining = BOOTSTRAP_TOTAL_MAX_CHARS - totalBootstrapChars;

                if (content.size() > remaining)
                {
                    content = ionclaw::util::StringHelper::utf8SafeTruncate(content, remaining);
                }

                totalBootstrapChars += content.size();

                // soul.md persona embodiment instruction
                if (filename == "SOUL.md")
                {
                    prompt << "\n\n# Persona\n"
                           << "Embody the persona and tone described below. "
                           << "Avoid stiff, generic replies; follow its guidance "
                           << "unless higher-priority instructions override it.\n\n";
                }
                else
                {
                    prompt << "\n\n# " << filename << "\n";
                }

                prompt << content;
            }
        }
        catch (const std::exception &e)
        {
            spdlog::error("[ContextBuilder] Failed to load bootstrap files: {}", e.what());
        }
    }

    // 12. memory context with recall instructions (full mode only, skip for lightweight channels)
    if (full && memory && !lightweight)
    {
        try
        {
            auto memoryContext = memory->getMemoryContext();

            if (!memoryContext.empty())
            {
                prompt << "\n\n# Memory\n"
                       << "Before answering questions about prior work, decisions, dates, people, "
                       << "preferences, or todos: search memory files first, then use the relevant "
                       << "information in your response. If low confidence after searching, say you checked.\n\n"
                       << memoryContext;
            }
        }
        catch (const std::exception &e)
        {
            spdlog::error("[ContextBuilder] Failed to load memory context: {}", e.what());
        }
    }

    // 13. always-on skills inline (full mode only, skip for lightweight channels)
    if (full && skillsLoader && !lightweight)
    {
        try
        {
            for (const auto &[skillName, skillContent] : skillsLoader->getAlwaysSkills())
            {
                prompt << "\n\n# Skill: " << skillName << "\n"
                       << skillContent;
            }
        }
        catch (const std::exception &e)
        {
            spdlog::error("[ContextBuilder] Failed to load always-on skills: {}", e.what());
        }
    }

    // 14. skills summary (full mode only, skip for lightweight channels)
    if (full && skillsLoader && !lightweight)
    {
        try
        {
            auto skillsSummary = skillsLoader->buildSkillsSummary();

            if (!skillsSummary.empty())
            {
                prompt << "\n\n# Skills\n"
                       << "Before replying, scan <available_skills> <description> entries.\n"
                       << "- If exactly one skill clearly applies: read its SKILL.md at "
                       << "<location> with read_file, then follow it.\n"
                       << "- If multiple could apply: choose the most specific one, "
                       << "then read and follow it.\n"
                       << "- If none clearly apply: do not read any SKILL.md.\n"
                       << "Skills marked always=\"true\" are already loaded above.\n\n"
                       << skillsSummary;
            }
        }
        catch (const std::exception &e)
        {
            spdlog::error("[ContextBuilder] Failed to build skills summary: {}", e.what());
        }
    }

    // 15. agent instructions from config
    if (!agentInstructions.empty())
    {
        prompt << "\n\n# Agent Instructions\n"
               << agentInstructions;
    }

    // 16. silent replies (full mode only)
    if (full)
    {
        prompt << "\n\n# Silent Replies\n"
               << "When you have nothing useful to say (e.g. a notification with no action needed, "
               << "or a subagent result that already arrived after your final answer), "
               << "reply with exactly [SILENT] as your entire message. "
               << "Do not wrap it in markdown or add any other text.";
    }

    return prompt.str();
}

std::string ContextBuilder::buildMediaAnnotation(const std::vector<nlohmann::json> &media)
{
    if (media.empty())
    {
        return "";
    }

    std::string annotation;

    for (const auto &m : media)
    {
        if (!m.is_string())
        {
            continue;
        }

        auto path = m.get<std::string>();

        // detect MIME type from extension
        std::string mime = "application/octet-stream";
        auto dot = path.rfind('.');

        if (dot != std::string::npos)
        {
            auto ext = path.substr(dot);
            ionclaw::util::StringHelper::toLowerInPlace(ext);

            if (ext == ".jpg" || ext == ".jpeg")
                mime = "image/jpeg";
            else if (ext == ".png")
                mime = "image/png";
            else if (ext == ".gif")
                mime = "image/gif";
            else if (ext == ".webp")
                mime = "image/webp";
            else if (ext == ".svg")
                mime = "image/svg+xml";
            else if (ext == ".bmp")
                mime = "image/bmp";
            else if (ext == ".mp3")
                mime = "audio/mpeg";
            else if (ext == ".wav")
                mime = "audio/wav";
            else if (ext == ".ogg" || ext == ".oga" || ext == ".opus")
                mime = "audio/ogg";
            else if (ext == ".m4a")
                mime = "audio/mp4";
            else if (ext == ".pdf")
                mime = "application/pdf";
        }

        if (mime.starts_with("audio/"))
        {
            annotation += "\n[audio attached: " + path + " (" + mime + ") — any transcription or failure note is included above]";
        }
        else if (mime.starts_with("image/"))
        {
            annotation += "\n[image attached: " + path + " — use vision tool with path=\"" + path + "\" to analyze]";
        }
        else
        {
            annotation += "\n[media attached: " + path + " (" + mime + ")]";
        }
    }

    return annotation;
}

std::vector<ionclaw::provider::Message> ContextBuilder::buildMessages(const std::string &systemPrompt, const std::vector<ionclaw::session::SessionMessage> &history, const std::string &userContent, const nlohmann::json &mediaBlocks, const std::map<int, nlohmann::json> &historyMediaBlocks)
{
    std::vector<ionclaw::provider::Message> messages;

    // system prompt
    ionclaw::provider::Message systemMsg;
    systemMsg.role = "system";
    systemMsg.content = systemPrompt;
    messages.push_back(systemMsg);

    // normalize history for the LLM: convert content blocks to plain text
    for (int idx = 0; idx < static_cast<int>(history.size()); ++idx)
    {
        const auto &msg = history[idx];
        ionclaw::provider::Message entry;
        entry.role = msg.role;

        // convert content blocks if raw field has them
        if (msg.raw.is_object() && msg.raw.contains("content"))
        {
            entry.content = contentToText(msg.raw["content"]);
        }
        else
        {
            entry.content = msg.content;
        }

        // restore tool_calls if present
        if (msg.raw.is_object() && msg.raw.contains("tool_calls") && msg.raw["tool_calls"].is_array())
        {
            for (const auto &tc : msg.raw["tool_calls"])
            {
                ionclaw::provider::ToolCall toolCall;
                toolCall.id = tc.contains("id") && tc["id"].is_string() ? tc["id"].get<std::string>() : "";

                if (tc.contains("function") && tc["function"].is_object())
                {
                    const auto &fn = tc["function"];
                    toolCall.name = fn.contains("name") && fn["name"].is_string() ? fn["name"].get<std::string>() : "";
                    auto argsStr = fn.contains("arguments") && fn["arguments"].is_string() ? fn["arguments"].get<std::string>() : "";

                    if (!argsStr.empty())
                    {
                        try
                        {
                            toolCall.arguments = nlohmann::json::parse(argsStr);
                        }
                        catch (const nlohmann::json::parse_error &)
                        {
                            toolCall.arguments = nlohmann::json::object();
                        }
                    }
                }

                entry.toolCalls.push_back(toolCall);
            }
        }

        // restore tool_call_id (type-check to avoid json::type_error on non-string values)
        if (msg.raw.is_object() && msg.raw.contains("tool_call_id") && msg.raw["tool_call_id"].is_string())
        {
            entry.toolCallId = msg.raw["tool_call_id"].get<std::string>();
        }

        // restore name
        if (msg.raw.is_object() && msg.raw.contains("name") && msg.raw["name"].is_string())
        {
            entry.name = msg.raw["name"].get<std::string>();
        }

        // attach media annotation and resolved image blocks for user messages with media
        if (msg.role == "user" && !msg.media.empty())
        {
            auto annotation = buildMediaAnnotation(msg.media);

            if (!annotation.empty())
            {
                entry.content += annotation;
            }

            // attach re-resolved image content blocks if available
            auto it = historyMediaBlocks.find(idx);

            if (it != historyMediaBlocks.end() && it->second.is_array() && !it->second.empty())
            {
                auto blocks = nlohmann::json::array();
                blocks.push_back({{"type", "text"}, {"text", entry.content}});

                for (const auto &block : it->second)
                {
                    blocks.push_back(block);
                }

                entry.contentBlocks = blocks;
            }
        }

        if (entry.role.empty() || (entry.content.empty() && entry.toolCalls.empty() && entry.role != "tool"))
        {
            continue;
        }

        messages.push_back(entry);
    }

    // build current user message
    ionclaw::provider::Message userMsg;
    userMsg.role = "user";
    userMsg.content = userContent;

    // attach pre-resolved media blocks if present
    if (mediaBlocks.is_array() && !mediaBlocks.empty())
    {
        auto blocks = nlohmann::json::array();
        blocks.push_back({{"type", "text"}, {"text", userContent}});

        for (const auto &block : mediaBlocks)
        {
            blocks.push_back(block);
        }

        userMsg.contentBlocks = blocks;
    }

    messages.push_back(userMsg);

    return messages;
}

// append a tool result message to the conversation
void ContextBuilder::addToolResult(std::vector<ionclaw::provider::Message> &messages, const std::string &toolCallId, const std::string &toolName, const std::string &result, const nlohmann::json &media)
{
    ionclaw::provider::Message msg;
    msg.role = "tool";
    msg.toolCallId = toolCallId;
    msg.name = toolName;
    msg.content = result;

    // attach structured media blocks if present (images, audio, etc.)
    if (media.is_array() && !media.empty())
    {
        auto blocks = nlohmann::json::array();

        for (const auto &block : media)
        {
            blocks.push_back(block);
        }

        // append text description as final block
        if (!result.empty())
        {
            blocks.push_back({{"type", "text"}, {"text", result}});
        }

        msg.contentBlocks = blocks;
    }

    messages.push_back(msg);
}

void ContextBuilder::addAssistantMessage(std::vector<ionclaw::provider::Message> &messages, const std::string &content, const std::vector<ionclaw::provider::ToolCall> &toolCalls, const std::string &reasoningContent, const nlohmann::json &reasoningBlocks)
{
    ionclaw::provider::Message msg;
    msg.role = "assistant";
    msg.content = content;
    msg.toolCalls = toolCalls;
    msg.reasoningContent = reasoningContent;
    msg.reasoningBlocks = reasoningBlocks;
    messages.push_back(msg);
}

std::string ContextBuilder::buildSubagentContext(int depth, int maxDepth)
{
    std::ostringstream ctx;

    // clang-format off
    ctx << "\n\n# Subagent Context\n"
        << "You are a subagent spawned by the parent agent for a specific task.\n"
        << "Depth: " << depth << "/" << maxDepth << ".\n"
        << "\n## Rules\n"
        << "1. Stay focused on your assigned task, nothing else.\n"
        << "2. Complete the task. Your final message is auto-delivered to the parent.\n"
        << "3. Do not ask clarifying questions; work with the information provided.\n"
        << "4. Do not initiate conversations, heartbeats, or proactive actions.\n"
        << "5. Do not pretend to be the parent agent.\n"
        << "\n## Output Format\n"
        << "Your final message should include:\n"
        << "- What you accomplished or found\n"
        << "- Relevant details the parent agent needs to know\n"
        << "- Keep it concise but informative\n"
        << "\n## Prohibitions\n"
        << "- Do NOT send messages to the user directly (parent agent's responsibility).\n"
        << "- Do NOT create cron jobs or persistent state.\n"
        << "- Do NOT use the message tool unless explicitly instructed in the task.\n";
    // clang-format on

    if (depth >= maxDepth)
    {
        ctx << "\n## Spawning\n"
            << "You are at maximum depth and CANNOT spawn further subagents.\n";
    }
    else
    {
        ctx << "\n## Spawning\n"
            << "You CAN spawn subagents if the task benefits from parallelization.\n"
            << "- Subagent results are auto-announced to you as user messages.\n"
            << "- Wait for all completion events before composing your final answer.\n"
            << "- Do NOT poll with subagents list in a loop.\n";
    }

    return ctx.str();
}

// detect error/diagnostic content in the tail of a tool result
bool ContextBuilder::hasImportantTail(const std::string &content, size_t scanBytes)
{
    static const std::vector<std::string> patterns = {
        "error",
        "Error",
        "ERROR",
        "failed",
        "Failed",
        "FAILED",
        "exception",
        "Exception",
        "stack trace",
        "Traceback",
        "WARN",
        "warning",
        "Warning",
        "fatal",
        "Fatal",
        "FATAL",
        "panic",
        "Panic",
        "assert",
        "Assert",
        "denied",
        "Denied",
        "not found",
        "Not Found",
    };

    auto start = content.size() > scanBytes ? content.size() - scanBytes : 0;
    auto tail = content.substr(start);

    for (const auto &pat : patterns)
    {
        if (tail.find(pat) != std::string::npos)
        {
            return true;
        }
    }

    return false;
}

void ContextBuilder::enforceToolResultBudget(std::vector<ionclaw::provider::Message> &messages, int maxTotalChars)
{
    // phase 1: cap any single tool result exceeding 50% of total context budget
    auto singleResultCap = std::max(400, maxTotalChars / 2);

    for (auto &msg : messages)
    {
        if (msg.role == "tool" && static_cast<int>(msg.content.size()) > singleResultCap)
        {
            // use larger tail budget if error content detected at the end
            auto importantTail = hasImportantTail(msg.content);
            auto tailRatio = importantTail ? 4 : 3; // 40% or 30%
            auto tailBudget = std::min(static_cast<int64_t>(singleResultCap) * tailRatio / 10, static_cast<int64_t>(4000));
            auto headBudget = std::max(static_cast<int64_t>(0), static_cast<int64_t>(singleResultCap) - tailBudget);

            auto safeTailBudget = std::min(static_cast<size_t>(tailBudget), msg.content.size());

            // utf-8 safe head truncation
            auto head = ionclaw::util::StringHelper::utf8SafeTruncate(msg.content, static_cast<size_t>(headBudget));

            // utf-8 safe tail extraction: skip continuation bytes at start
            auto tailStart = msg.content.size() - safeTailBudget;

            while (tailStart < msg.content.size() && (static_cast<unsigned char>(msg.content[tailStart]) & 0xC0) == 0x80)
            {
                tailStart++;
            }

            auto tail = msg.content.substr(tailStart);

            auto lineStart = tail.find('\n');

            if (lineStart != std::string::npos && lineStart < static_cast<size_t>(tailBudget / 3))
            {
                tail = tail.substr(lineStart + 1);
            }

            msg.content = head + "\n[... truncated for context budget ...]\n" + tail;
        }
    }

    // phase 2: if total still exceeds budget, compact oldest tool results first
    int totalToolChars = 0;
    int toolResultCount = 0;

    for (const auto &msg : messages)
    {
        if (msg.role == "tool")
        {
            totalToolChars += static_cast<int>(msg.content.size());
            toolResultCount++;
        }
    }

    if (totalToolChars <= maxTotalChars || toolResultCount == 0)
    {
        return;
    }

    // progressive compaction: compact oldest tool results first until budget met
    int excess = totalToolChars - maxTotalChars;

    for (auto &msg : messages)
    {
        if (excess <= 0)
        {
            break;
        }

        if (msg.role != "tool")
        {
            continue;
        }

        auto currentSize = static_cast<int>(msg.content.size());
        auto minSize = 200; // keep at least a brief note

        if (currentSize > minSize)
        {
            auto targetSize = std::max(minSize, currentSize - excess);
            auto headSize = targetSize * 3 / 4;
            auto tailSize = std::min(targetSize / 4, 500);

            auto safeTailSize = std::min(static_cast<size_t>(tailSize), msg.content.size());

            // utf-8 safe head truncation
            auto head = ionclaw::util::StringHelper::utf8SafeTruncate(msg.content, static_cast<size_t>(headSize));

            // utf-8 safe tail extraction: skip continuation bytes at start
            auto tailStart = msg.content.size() - safeTailSize;

            while (tailStart < msg.content.size() && (static_cast<unsigned char>(msg.content[tailStart]) & 0xC0) == 0x80)
            {
                tailStart++;
            }

            auto tail = msg.content.substr(tailStart);

            auto compacted = head + "\n[... tool output compacted ...]\n" + tail;

            // only apply when it actually shrinks the message, otherwise a near-minimum result would grow and inflate the remaining excess
            if (static_cast<int>(compacted.size()) < currentSize)
            {
                excess -= (currentSize - static_cast<int>(compacted.size()));
                msg.content = std::move(compacted);
            }
        }
    }

    spdlog::info("[ContextBuilder] Enforced tool result budget: {}→{} chars across {} results", totalToolChars, maxTotalChars, toolResultCount);
}

void ContextBuilder::stripThinkingFromHistory(std::vector<ionclaw::provider::Message> &messages)
{
    // find the last assistant message index
    int lastAssistantIdx = -1;

    for (int i = static_cast<int>(messages.size()) - 1; i >= 0; --i)
    {
        if (messages[i].role == "assistant")
        {
            lastAssistantIdx = i;
            break;
        }
    }

    for (int i = 0; i < static_cast<int>(messages.size()); ++i)
    {
        if (i != lastAssistantIdx && !messages[i].reasoningContent.empty())
        {
            messages[i].reasoningContent.clear();
        }
    }
}

void ContextBuilder::pruneHistoryImages(std::vector<ionclaw::provider::Message> &messages, int keepRecent)
{
    // count user messages from the end to determine which ones are "recent"
    int userCount = 0;
    int cutoffIdx = 0; // default: don't prune anything

    for (int i = static_cast<int>(messages.size()) - 1; i >= 0; --i)
    {
        if (messages[i].role == "user")
        {
            userCount++;

            if (userCount >= keepRecent)
            {
                cutoffIdx = i;
                break;
            }
        }
    }

    for (int i = 0; i < cutoffIdx; ++i)
    {
        if (!messages[i].contentBlocks.is_array())
        {
            continue;
        }

        bool hasImage = false;
        nlohmann::json pruned = nlohmann::json::array();

        for (const auto &block : messages[i].contentBlocks)
        {
            auto type = block.is_object() && block.contains("type") && block["type"].is_string() ? block["type"].get<std::string>() : "";

            if (type == "image" || type == "image_url")
            {
                pruned.push_back({{"type", "text"}, {"text", "[image data pruned from context — use the vision tool to re-examine the file if needed]"}});
                hasImage = true;
            }
            else
            {
                pruned.push_back(block);
            }
        }

        if (hasImage)
        {
            messages[i].contentBlocks = pruned;
        }
    }
}

// repair tool use / result pairing after history trimming:
// - insert synthetic error results for tool calls with no matching result
// - drop duplicate tool results (same tool_call_id)
void ContextBuilder::repairToolUseResultPairing(std::vector<ionclaw::provider::Message> &messages)
{
    // collect all tool call IDs from assistant messages
    std::set<std::string> toolCallIds;

    for (const auto &msg : messages)
    {
        if (msg.role == "assistant")
        {
            for (const auto &tc : msg.toolCalls)
            {
                if (!tc.id.empty())
                {
                    toolCallIds.insert(tc.id);
                }
            }
        }
    }

    // collect tool result IDs present in the transcript
    std::set<std::string> seenResultIds;
    std::vector<ionclaw::provider::Message> repaired;
    repaired.reserve(messages.size());

    for (auto &msg : messages)
    {
        if (msg.role == "tool")
        {
            // drop orphaned tool results with no matching tool call
            if (toolCallIds.find(msg.toolCallId) == toolCallIds.end())
            {
                spdlog::debug("[ContextBuilder] Dropping orphaned tool result (id={})", msg.toolCallId);
                continue;
            }

            // drop duplicate tool results
            if (seenResultIds.contains(msg.toolCallId))
            {
                spdlog::debug("[ContextBuilder] Dropping duplicate tool result (id={})", msg.toolCallId);
                continue;
            }

            seenResultIds.insert(msg.toolCallId);
        }

        repaired.push_back(std::move(msg));
    }

    // insert synthetic error results for unmatched tool calls
    // scan assistant messages and inject missing results right after their tool result block
    std::vector<ionclaw::provider::Message> finalMessages;
    finalMessages.reserve(repaired.size() + toolCallIds.size());

    for (size_t i = 0; i < repaired.size(); ++i)
    {
        finalMessages.push_back(repaired[i]);

        if (repaired[i].role != "assistant" || repaired[i].toolCalls.empty())
        {
            continue;
        }

        // find the end of the tool result block following this assistant message
        size_t resultEnd = i + 1;

        while (resultEnd < repaired.size() && repaired[resultEnd].role == "tool")
        {
            resultEnd++;
        }

        // copy existing tool results
        for (size_t j = i + 1; j < resultEnd; ++j)
        {
            finalMessages.push_back(repaired[j]);
        }

        // inject synthetic results for missing tool calls
        for (const auto &tc : repaired[i].toolCalls)
        {
            if (seenResultIds.find(tc.id) == seenResultIds.end())
            {
                ionclaw::provider::Message synthetic;
                synthetic.role = "tool";
                synthetic.toolCallId = tc.id;
                synthetic.name = tc.name;
                synthetic.content = "[tool result unavailable — trimmed from history]";
                finalMessages.push_back(synthetic);
                spdlog::debug("[ContextBuilder] Inserted synthetic result for unmatched tool call (id={}, name={})", tc.id, tc.name);
            }
        }

        // skip the tool results we already copied
        i = resultEnd - 1;
    }

    messages = std::move(finalMessages);
}

std::string ContextBuilder::readProjectFile(const std::string &filename) const
{
    if (config.projectPath.empty())
    {
        return "";
    }

    auto filePath = fs::path(config.projectPath) / filename;

    if (!fs::exists(filePath) || !fs::is_regular_file(filePath))
    {
        return "";
    }

    std::ifstream file(filePath, std::ios::binary);

    if (!file.is_open())
    {
        spdlog::warn("[ContextBuilder] Cannot open project file: {}", filePath.string());
        return "";
    }

    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

} // namespace agent
} // namespace ionclaw
