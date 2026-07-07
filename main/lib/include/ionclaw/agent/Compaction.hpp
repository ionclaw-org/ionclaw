#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ionclaw/provider/LlmProvider.hpp"
#include "nlohmann/json.hpp"

namespace ionclaw
{
namespace agent
{

enum class IdentifierPolicy
{
    Strict,
    Relaxed,
    Off,
};

enum class CompactionFailure
{
    None,
    Timeout,
    ProviderError,
    EmptyResult,
    TooFewMessages,
};

struct CompactionConfig
{
    IdentifierPolicy identifierPolicy = IdentifierPolicy::Relaxed;
    int chunkSize = 20;
    int timeoutMs = 30000;
    bool preserveActiveTasks = true;
    bool preserveLastUserRequest = true;
    bool preserveDecisions = true;
};

struct CompactionResult
{
    std::vector<ionclaw::provider::Message> messages;
    CompactionFailure failure = CompactionFailure::None;
    std::string failureReason;
    int summarizedCount = 0;
    int keptCount = 0;
};

class Compaction
{
public:
    static CompactionResult compactWithResult(const std::vector<ionclaw::provider::Message> &messages, std::shared_ptr<ionclaw::provider::LlmProvider> provider, const std::string &model, const nlohmann::json &modelParams = nlohmann::json(), const CompactionConfig &config = CompactionConfig());
    static std::vector<ionclaw::provider::Message> pruneHistoryForContextShare(const std::vector<ionclaw::provider::Message> &messages, const std::string &model, const nlohmann::json &modelParams = nlohmann::json(), double maxHistoryShare = 0.5);

private:
    static constexpr double COMPACT_RATIO = 0.4;
    static constexpr int MAX_TOOL_RESULT_IN_SUMMARY = 2000;
    static constexpr int SUMMARY_MAX_RETRIES = 3;
    static constexpr int SUMMARY_RETRY_BASE_MS = 500;
    static constexpr int SUMMARY_RETRY_MAX_MS = 5000;
    static constexpr double SUMMARY_RETRY_JITTER = 0.2;

    static std::string buildSummaryPrompt(const CompactionConfig &config);
    static std::string messagesToText(const std::vector<ionclaw::provider::Message> &messages);
    static int computeRetryDelay(int attempt);
    static std::string generateSummary(const std::vector<ionclaw::provider::Message> &messages, std::shared_ptr<ionclaw::provider::LlmProvider> provider, const std::string &model, const nlohmann::json &modelParams, const CompactionConfig &config);

    static constexpr double MIN_CHUNK_RATIO = 0.15;
    static constexpr double BASE_CHUNK_RATIO = 0.4;
    static constexpr int SUMMARIZATION_OVERHEAD_TOKENS = 4096;
    static constexpr double SAFETY_MARGIN = 1.2;

    static int computeAdaptiveChunkSize(const std::vector<ionclaw::provider::Message> &messages, const std::string &model, const nlohmann::json &modelParams);
    static std::string generateChunkedSummary(const std::vector<ionclaw::provider::Message> &messages, std::shared_ptr<ionclaw::provider::LlmProvider> provider, const std::string &model, const nlohmann::json &modelParams, const CompactionConfig &config);
    static std::string truncationFallback(const std::vector<ionclaw::provider::Message> &messages);
    static bool hasRealConversationContent(const std::vector<ionclaw::provider::Message> &conversation);
};

} // namespace agent
} // namespace ionclaw
