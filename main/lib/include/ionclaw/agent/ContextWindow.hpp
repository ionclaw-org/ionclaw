#pragma once

#include <map>
#include <string>
#include <vector>

#include "ionclaw/provider/LlmProvider.hpp"
#include "nlohmann/json.hpp"

namespace ionclaw
{
namespace agent
{

enum class ContextGuardLevel
{
    Ok,
    Warning,
    Critical,
};

struct ContextGuardResult
{
    ContextGuardLevel level = ContextGuardLevel::Ok;
    int estimatedTokens = 0;
    int modelLimit = 0;
    double usageRatio = 0.0;
};

class ContextWindow
{
public:
    static int getModelLimit(const std::string &model, const nlohmann::json &modelParams = nlohmann::json(), int contextTokensCap = 0);
    static int estimateTokens(const std::vector<ionclaw::provider::Message> &messages);
    static std::vector<ionclaw::provider::Message> trimHistory(
        const std::vector<ionclaw::provider::Message> &messages, int maxHistory);
    static bool needsCompaction(
        const std::vector<ionclaw::provider::Message> &messages,
        const std::string &model,
        const nlohmann::json &modelParams = nlohmann::json(),
        int contextTokensCap = 0);
    static ContextGuardResult checkMinContext(
        const std::vector<ionclaw::provider::Message> &messages,
        const std::string &model,
        const nlohmann::json &modelParams = nlohmann::json(),
        int contextTokensCap = 0);

private:
    static const std::map<std::string, int> MODEL_CONTEXT_LIMITS;
    static constexpr int DEFAULT_LIMIT = 100000;
    static constexpr double SAFETY_MARGIN = 0.85;
    static constexpr double WARNING_THRESHOLD = 0.90;
    static constexpr double CRITICAL_THRESHOLD = 0.95;
    static constexpr int MIN_RESPONSE_TOKENS = 1024;
};

} // namespace agent
} // namespace ionclaw
