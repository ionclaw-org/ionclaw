#pragma once

#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace ionclaw
{
namespace agent
{

enum class LoopSeverity
{
    None,
    Warning,
    Critical,
    CircuitBreaker
};

struct LoopDetectionResult
{
    LoopSeverity severity = LoopSeverity::None;
    std::string detector;
    std::string reason;
    bool shouldStop = false;
};

struct ToolLoopConfig
{
    int historySize = 30;
    int warningThreshold = 10;
    int criticalThreshold = 20;
    int circuitBreakerThreshold = 30;
    bool enableGenericRepeat = true;
    bool enablePollNoProgress = true;
    bool enablePingPong = true;
};

class ToolLoopDetector
{
public:
    static constexpr int HISTORY_SIZE = 30;
    static constexpr int WARNING_THRESHOLD = 10;
    static constexpr int CRITICAL_THRESHOLD = 20;
    static constexpr int GLOBAL_CIRCUIT_BREAKER = 30;

    ToolLoopDetector() = default;
    explicit ToolLoopDetector(const ToolLoopConfig &cfg)
        : loopConfig(cfg)
    {
    }

    void recordCall(const std::string &toolName, const std::string &argsHash);
    void recordResult(const std::string &toolName, const std::string &resultHash);
    LoopDetectionResult check() const;
    bool shouldEmitWarning(const std::string &warningKey, int count);
    void reset();

    static std::string hashString(const std::string &input);

private:
    struct HistoryEntry
    {
        std::string toolName;
        std::string argsHash;
        std::string signature; // toolName + argsHash combined
        std::string resultHash;
    };

    static constexpr int WARNING_BUCKET_SIZE = 10;
    static constexpr int MAX_WARNING_KEYS = 256;

    ToolLoopConfig loopConfig;
    std::deque<HistoryEntry> history;
    std::map<std::string, int> signatureCounts;
    std::map<std::string, int> warningBuckets;

    static const std::set<std::string> KNOWN_POLL_TOOLS;

    LoopDetectionResult checkGenericRepeat() const;
    LoopDetectionResult checkKnownPollNoProgress() const;
    LoopDetectionResult checkPingPong() const;
    LoopDetectionResult checkGlobalNoProgress() const;
};

} // namespace agent
} // namespace ionclaw
