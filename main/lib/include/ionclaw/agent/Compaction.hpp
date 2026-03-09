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
    static std::vector<ionclaw::provider::Message> compact(
        const std::vector<ionclaw::provider::Message> &messages,
        std::shared_ptr<ionclaw::provider::LlmProvider> provider,
        const std::string &model,
        const nlohmann::json &modelParams = nlohmann::json(),
        const CompactionConfig &config = CompactionConfig());

    static CompactionResult compactWithResult(
        const std::vector<ionclaw::provider::Message> &messages,
        std::shared_ptr<ionclaw::provider::LlmProvider> provider,
        const std::string &model,
        const nlohmann::json &modelParams = nlohmann::json(),
        const CompactionConfig &config = CompactionConfig());

    // drop oldest message chunks until history fits within maxHistoryShare of context window
    static std::vector<ionclaw::provider::Message> pruneHistoryForContextShare(
        const std::vector<ionclaw::provider::Message> &messages,
        const std::string &model,
        const nlohmann::json &modelParams = nlohmann::json(),
        double maxHistoryShare = 0.5);
};

} // namespace agent
} // namespace ionclaw
