#include "ionclaw/agent/Compaction.hpp"

#include <algorithm>
#include <future>
#include <random>
#include <sstream>
#include <thread>

#include "ionclaw/agent/ContextWindow.hpp"
#include "ionclaw/provider/ProviderHelper.hpp"
#include "ionclaw/util/StringHelper.hpp"
#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace agent
{

// build summary prompt with dynamic preserve instructions
std::string Compaction::buildSummaryPrompt(const CompactionConfig &config)
{
    std::ostringstream prompt;
    prompt << "Summarize the conversation below into a concise recap. Preserve:\n";
    prompt << "- Key decisions and conclusions\n";

    if (config.identifierPolicy != IdentifierPolicy::Off)
    {
        if (config.identifierPolicy == IdentifierPolicy::Strict)
        {
            prompt << "- ALL file paths, variable names, identifiers, URLs, class names, function names (preserve exactly)\n";
        }
        else
        {
            prompt << "- File paths, variable names, identifiers, URLs\n";
        }
    }

    prompt << "- Errors encountered and how they were resolved\n";

    if (config.preserveActiveTasks)
    {
        prompt << "- Active tasks, batch progress, and pending work items\n";
    }

    if (config.preserveLastUserRequest)
    {
        prompt << "- The last user request and actions taken in response\n";
    }

    if (config.preserveDecisions)
    {
        prompt << "- Decisions, rationale, commitments, and constraints\n";
        prompt << "- TODOs and open questions\n";
    }

    prompt << "- User preferences and requirements\n";
    prompt << "- Media file references (preserve [media: ...] annotations with exact file paths)\n\n";
    prompt << "Be concise but do not omit important technical details. ";
    prompt << "Write the summary as a single block of text, not a conversation.";

    return prompt.str();
}

// convert messages to flat text for summarization
std::string Compaction::messagesToText(const std::vector<ionclaw::provider::Message> &messages)
{
    std::ostringstream parts;

    for (const auto &msg : messages)
    {
        auto content = msg.content;

        if (msg.role == "tool" && content.size() > MAX_TOOL_RESULT_IN_SUMMARY)
        {
            auto toolName = msg.name.empty() ? "tool" : msg.name;
            content = ionclaw::util::StringHelper::utf8SafeTruncate(content, MAX_TOOL_RESULT_IN_SUMMARY) +
                      "\n[" + toolName + " output truncated]";
        }

        if (!content.empty())
        {
            parts << "[" << msg.role << "]: " << content << "\n\n";
        }
    }

    return parts.str();
}

// compute retry delay with exponential backoff and jitter
int Compaction::computeRetryDelay(int attempt)
{
    static thread_local std::mt19937 rng(std::random_device{}());

    auto delayMs = std::min(SUMMARY_RETRY_BASE_MS << attempt, SUMMARY_RETRY_MAX_MS);
    auto jitterRange = static_cast<int>(delayMs * SUMMARY_RETRY_JITTER);
    std::uniform_int_distribution<int> jitter(-jitterRange, jitterRange);
    return delayMs + jitter(rng);
}

// generate a conversation summary via llm with retry and fallback
std::string Compaction::generateSummary(
    const std::vector<ionclaw::provider::Message> &messages,
    std::shared_ptr<ionclaw::provider::LlmProvider> provider,
    const std::string &model,
    const nlohmann::json &modelParams,
    const CompactionConfig &config)
{
    auto text = messagesToText(messages);
    auto prompt = buildSummaryPrompt(config);

    ionclaw::provider::ChatCompletionRequest request;
    request.model = model;
    request.maxTokens = 2048;
    request.temperature = 0.3;
    request.modelParams = modelParams;

    ionclaw::provider::Message systemMsg;
    systemMsg.role = "system";
    systemMsg.content = prompt;
    request.messages.push_back(systemMsg);

    ionclaw::provider::Message userMsg;
    userMsg.role = "user";
    userMsg.content = text;
    request.messages.push_back(userMsg);

    // tier 1: full summarization with retries
    for (int attempt = 0; attempt < SUMMARY_MAX_RETRIES; ++attempt)
    {
        try
        {
            auto response = provider->chat(request);
            return response.content.empty() ? "[summary unavailable]" : response.content;
        }
        catch (const std::exception &e)
        {
            auto category = ionclaw::provider::ProviderHelper::classifyError(e.what());
            spdlog::warn("[Compaction] Summary attempt {}/{} failed ({}): {}",
                         attempt + 1, SUMMARY_MAX_RETRIES, category, e.what());

            if (category == "context_overflow" && messages.size() > 3)
            {
                // tier 2: retry excluding oversized messages
                std::vector<ionclaw::provider::Message> trimmed;
                for (const auto &msg : messages)
                {
                    if (msg.role == "tool" && msg.content.size() > 5000)
                    {
                        continue; // skip oversized tool results
                    }
                    trimmed.push_back(msg);
                }

                if (trimmed.size() < messages.size())
                {
                    spdlog::info("[Compaction] Retrying summary excluding {} oversized messages",
                                 messages.size() - trimmed.size());
                    request.messages.back().content = messagesToText(trimmed);

                    try
                    {
                        auto response = provider->chat(request);
                        return response.content.empty() ? "[summary unavailable]" : response.content;
                    }
                    catch (const std::exception &e)
                    {
                        spdlog::warn("[Compaction] Compaction attempt failed: {}", e.what());
                    }
                }

                // tier 3: text-only note listing message count
                std::ostringstream note;
                note << "[Summarization failed due to oversized content. ";
                note << "Conversation contained " << messages.size() << " messages";

                // extract key identifiers from the last few messages
                auto start = messages.size() > 5 ? messages.size() - 5 : 0;
                note << ". Recent context: ";
                for (auto i = start; i < messages.size(); ++i)
                {
                    auto content = messages[i].content;
                    if (content.size() > 200)
                    {
                        content = ionclaw::util::StringHelper::utf8SafeTruncate(content, 200) + "...";
                    }
                    if (!content.empty())
                    {
                        note << "[" << messages[i].role << "]: " << content << " ";
                    }
                }
                note << "]";
                return note.str();
            }

            if (attempt < SUMMARY_MAX_RETRIES - 1)
            {
                auto delay = computeRetryDelay(attempt);
                spdlog::info("[Compaction] Retrying in {}ms", delay);
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            }
        }
    }

    return "[summary unavailable after " + std::to_string(SUMMARY_MAX_RETRIES) + " attempts]";
}

int Compaction::computeAdaptiveChunkSize(
    const std::vector<ionclaw::provider::Message> &messages,
    const std::string &model,
    const nlohmann::json &modelParams)
{
    auto modelLimit = ionclaw::agent::ContextWindow::getModelLimit(model, modelParams);
    auto totalTokens = ionclaw::agent::ContextWindow::estimateTokens(messages);

    if (totalTokens == 0 || messages.empty())
    {
        return 20; // fallback
    }

    auto avgTokensPerMsg = totalTokens / static_cast<int>(messages.size());
    auto contextPerMsg = static_cast<double>(avgTokensPerMsg) / modelLimit;

    // when messages are large relative to context, use smaller chunks
    double ratio = BASE_CHUNK_RATIO;

    if (contextPerMsg > 0.01)
    {
        ratio = std::max(MIN_CHUNK_RATIO, BASE_CHUNK_RATIO - contextPerMsg * 10);
    }

    // compute max messages per chunk that fit the model's summarization budget
    auto availableTokens = static_cast<int>((modelLimit - SUMMARIZATION_OVERHEAD_TOKENS) / SAFETY_MARGIN);
    auto maxMsgsPerChunk = std::max(2, availableTokens / std::max(1, avgTokensPerMsg));

    auto ratioBasedSize = std::max(2, static_cast<int>(messages.size() * ratio));
    return std::min(ratioBasedSize, maxMsgsPerChunk);
}

// chunked summarization: split into token-aware chunks, summarize each, then merge
std::string Compaction::generateChunkedSummary(
    const std::vector<ionclaw::provider::Message> &messages,
    std::shared_ptr<ionclaw::provider::LlmProvider> provider,
    const std::string &model,
    const nlohmann::json &modelParams,
    const CompactionConfig &config)
{
    auto chunkSize = computeAdaptiveChunkSize(messages, model, modelParams);
    auto total = static_cast<int>(messages.size());

    spdlog::debug("[Compaction] Adaptive chunk size: {} (total msgs: {})", chunkSize, total);

    if (total <= chunkSize)
    {
        return generateSummary(messages, provider, model, modelParams, config);
    }

    // split into chunks and summarize each
    std::vector<std::string> chunkSummaries;

    for (int i = 0; i < total; i += chunkSize)
    {
        auto end = std::min(i + chunkSize, total);
        std::vector<ionclaw::provider::Message> chunk(messages.begin() + i, messages.begin() + end);
        auto summary = generateSummary(chunk, provider, model, modelParams, config);
        chunkSummaries.push_back(summary);
    }

    if (chunkSummaries.size() == 1)
    {
        return chunkSummaries[0];
    }

    // merge summaries in a final pass
    std::ostringstream merged;

    for (size_t i = 0; i < chunkSummaries.size(); ++i)
    {
        merged << "--- Part " << (i + 1) << " ---\n"
               << chunkSummaries[i] << "\n\n";
    }

    ionclaw::provider::ChatCompletionRequest mergeRequest;
    mergeRequest.model = model;
    mergeRequest.maxTokens = 2048;
    mergeRequest.temperature = 0.3;
    mergeRequest.modelParams = modelParams;

    ionclaw::provider::Message systemMsg;
    systemMsg.role = "system";
    systemMsg.content =
        "Merge the following partial summaries into a single cohesive summary.\n"
        "MUST PRESERVE:\n"
        "- Active tasks and their current status\n"
        "- Batch operation progress (e.g., '5/17 items completed')\n"
        "- The last user request and actions taken in response\n"
        "- TODOs, open questions, commitments, and constraints\n"
        "- All opaque identifiers exactly as written: UUIDs, hashes, IDs, tokens, "
        "API keys, hostnames, IPs, ports, URLs, and file names";
    mergeRequest.messages.push_back(systemMsg);

    ionclaw::provider::Message userMsg;
    userMsg.role = "user";
    userMsg.content = merged.str();
    mergeRequest.messages.push_back(userMsg);

    auto response = provider->chat(mergeRequest);
    return response.content.empty() ? chunkSummaries.back() : response.content;
}

// simple truncation fallback when summarization fails or times out
std::string Compaction::truncationFallback(const std::vector<ionclaw::provider::Message> &messages)
{
    std::ostringstream text;
    text << "[Conversation truncated due to summarization timeout]\n";

    // keep last few messages as context
    auto start = messages.size() > 5 ? messages.size() - 5 : 0;

    for (auto i = start; i < messages.size(); ++i)
    {
        auto content = messages[i].content;

        if (content.size() > 500)
        {
            content = ionclaw::util::StringHelper::utf8SafeTruncate(content, 500) + "...";
        }

        if (!content.empty())
        {
            text << "[" << messages[i].role << "]: " << content << "\n";
        }
    }

    return text.str();
}

// check if conversation has real content worth compacting
bool Compaction::hasRealConversationContent(const std::vector<ionclaw::provider::Message> &conversation)
{
    for (const auto &msg : conversation)
    {
        if (msg.role == "user" || msg.role == "assistant")
        {
            return true;
        }
    }

    return false;
}

// compact conversation by summarizing older messages
std::vector<ionclaw::provider::Message> Compaction::compact(
    const std::vector<ionclaw::provider::Message> &messages,
    std::shared_ptr<ionclaw::provider::LlmProvider> provider,
    const std::string &model,
    const nlohmann::json &modelParams,
    const CompactionConfig &config)
{
    return compactWithResult(messages, provider, model, modelParams, config).messages;
}

CompactionResult Compaction::compactWithResult(
    const std::vector<ionclaw::provider::Message> &messages,
    std::shared_ptr<ionclaw::provider::LlmProvider> provider,
    const std::string &model,
    const nlohmann::json &modelParams,
    const CompactionConfig &config)
{
    CompactionResult result;

    // separate system messages from conversation
    std::vector<ionclaw::provider::Message> systemMsgs;
    std::vector<ionclaw::provider::Message> conversation;

    for (const auto &msg : messages)
    {
        if (msg.role == "system")
        {
            systemMsgs.push_back(msg);
        }
        else
        {
            conversation.push_back(msg);
        }
    }

    // skip compaction if no real conversation content
    if (!hasRealConversationContent(conversation))
    {
        result.messages = messages;
        result.failure = CompactionFailure::TooFewMessages;
        result.failureReason = "no real conversation content";
        return result;
    }

    auto cutPoint = std::max(2, static_cast<int>(conversation.size() * COMPACT_RATIO));

    if (cutPoint >= static_cast<int>(conversation.size()))
    {
        result.messages = messages;
        result.failure = CompactionFailure::TooFewMessages;
        result.failureReason = "not enough messages to compact";
        return result;
    }

    std::vector<ionclaw::provider::Message> toSummarize(conversation.begin(), conversation.begin() + cutPoint);
    std::vector<ionclaw::provider::Message> toKeep(conversation.begin() + cutPoint, conversation.end());

    {
        size_t danglingCount = 0;
        while (danglingCount < toKeep.size() && toKeep[danglingCount].role == "tool")
        {
            ++danglingCount;
        }

        if (danglingCount > 0)
        {
            toSummarize.insert(toSummarize.end(),
                               std::make_move_iterator(toKeep.begin()),
                               std::make_move_iterator(toKeep.begin() + static_cast<ptrdiff_t>(danglingCount)));
            toKeep.erase(toKeep.begin(), toKeep.begin() + static_cast<ptrdiff_t>(danglingCount));
        }
    }

    if (toSummarize.empty())
    {
        result.messages = messages;
        result.failure = CompactionFailure::TooFewMessages;
        result.failureReason = "nothing to summarize";
        return result;
    }

    try
    {
        std::string summary;
        bool timedOut = false;

        if (config.timeoutMs > 0)
        {
            // use promise+thread instead of std::async to avoid blocking destructor on timeout
            auto promise = std::make_shared<std::promise<std::string>>();
            auto future = promise->get_future();

            // capture by value to prevent dangling references
            auto capturedSummarize = toSummarize;
            auto capturedProvider = provider;
            auto capturedModel = model;
            auto capturedParams = modelParams;
            auto capturedConfig = config;

            std::thread([promise, capturedSummarize, capturedProvider, capturedModel, capturedParams, capturedConfig]()
                        {
                try
                {
                    std::string result;
                    if (static_cast<int>(capturedSummarize.size()) > capturedConfig.chunkSize)
                    {
                        result = generateChunkedSummary(capturedSummarize, capturedProvider, capturedModel, capturedParams, capturedConfig);
                    }
                    else
                    {
                        result = generateSummary(capturedSummarize, capturedProvider, capturedModel, capturedParams, capturedConfig);
                    }
                    promise->set_value(std::move(result));
                }
                catch (...)
                {
                    promise->set_exception(std::current_exception());
                } })
                .detach();

            auto status = future.wait_for(std::chrono::milliseconds(config.timeoutMs));

            if (status == std::future_status::ready)
            {
                summary = future.get();
            }
            else
            {
                timedOut = true;
                summary = truncationFallback(toSummarize);
            }
        }
        else
        {
            if (static_cast<int>(toSummarize.size()) > config.chunkSize)
            {
                summary = generateChunkedSummary(toSummarize, provider, model, modelParams, config);
            }
            else
            {
                summary = generateSummary(toSummarize, provider, model, modelParams, config);
            }
        }

        if (summary.empty() || summary == "[summary unavailable]")
        {
            result.messages = messages;
            result.failure = CompactionFailure::EmptyResult;
            result.failureReason = "summarization returned empty result";
            return result;
        }

        // reassemble
        result.messages.reserve(systemMsgs.size() + 2 + toKeep.size());
        result.messages.insert(result.messages.end(), systemMsgs.begin(), systemMsgs.end());

        ionclaw::provider::Message summaryMsg;
        summaryMsg.role = "user";
        summaryMsg.content = "[Previous conversation summary]:\n" + summary;
        result.messages.push_back(summaryMsg);

        ionclaw::provider::Message ackMsg;
        ackMsg.role = "assistant";
        ackMsg.content = "Understood. I have the context from our previous conversation. How can I help you next?";
        result.messages.push_back(ackMsg);

        result.messages.insert(result.messages.end(), toKeep.begin(), toKeep.end());
        result.summarizedCount = static_cast<int>(toSummarize.size());
        result.keptCount = static_cast<int>(toKeep.size());

        spdlog::info("[Compaction] Compacted {} messages into summary ({} chars), keeping {} recent",
                     toSummarize.size(), summary.size(), toKeep.size());

        if (timedOut)
        {
            result.failure = CompactionFailure::Timeout;
            result.failureReason = "summarization timed out, used truncation fallback";
        }

        return result;
    }
    catch (const std::exception &e)
    {
        result.messages = messages;
        result.failure = CompactionFailure::ProviderError;
        result.failureReason = e.what();
        return result;
    }
}

std::vector<ionclaw::provider::Message> Compaction::pruneHistoryForContextShare(
    const std::vector<ionclaw::provider::Message> &messages,
    const std::string &model,
    const nlohmann::json &modelParams,
    double maxHistoryShare)
{
    auto modelLimit = ContextWindow::getModelLimit(model, modelParams);
    auto budget = static_cast<int>(modelLimit * maxHistoryShare);

    // separate system messages from conversation
    std::vector<ionclaw::provider::Message> systemMsgs;
    std::vector<ionclaw::provider::Message> conversation;

    for (const auto &msg : messages)
    {
        if (msg.role == "system")
        {
            systemMsgs.push_back(msg);
        }
        else
        {
            conversation.push_back(msg);
        }
    }

    auto conversationTokens = ContextWindow::estimateTokens(conversation);

    if (conversationTokens <= budget)
    {
        return messages;
    }

    // drop oldest halves iteratively until conversation fits within budget
    static constexpr int PARTS = 2;
    int droppedChunks = 0;
    size_t startIdx = 0;

    while (conversationTokens > budget && (conversation.size() - startIdx) > static_cast<size_t>(PARTS))
    {
        auto remaining = conversation.size() - startIdx;
        auto chunkSize = std::max(size_t{1}, remaining / PARTS);

        // drop the oldest chunk by advancing start index
        startIdx += chunkSize;
        droppedChunks++;

        // skip orphaned tool results at the new start
        while (startIdx < conversation.size() && conversation[startIdx].role == "tool")
        {
            ++startIdx;
        }

        // re-estimate tokens on the kept portion
        std::vector<ionclaw::provider::Message> kept(conversation.begin() + static_cast<ptrdiff_t>(startIdx), conversation.end());
        conversationTokens = ContextWindow::estimateTokens(kept);
    }

    // apply the accumulated drops in a single erase
    if (startIdx > 0)
    {
        conversation.erase(conversation.begin(), conversation.begin() + static_cast<ptrdiff_t>(startIdx));
    }

    if (droppedChunks > 0)
    {
        spdlog::info("[Compaction] Pruned {} chunks to fit history within {:.0f}% context share ({} tokens remaining)",
                     droppedChunks, maxHistoryShare * 100, conversationTokens);
    }

    // reassemble
    std::vector<ionclaw::provider::Message> result;
    result.reserve(systemMsgs.size() + conversation.size());
    result.insert(result.end(), systemMsgs.begin(), systemMsgs.end());
    result.insert(result.end(), conversation.begin(), conversation.end());
    return result;
}

} // namespace agent
} // namespace ionclaw
