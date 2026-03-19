#include "ionclaw/agent/ToolLoopDetector.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <sstream>

#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace agent
{

const std::set<std::string> ToolLoopDetector::KNOWN_POLL_TOOLS = {
    "exec", "http_client", "web_fetch", "browser"};

std::string ToolLoopDetector::hashString(const std::string &input)
{
    uint64_t hash = 14695981039346656037ULL;

    for (auto c : input)
    {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ULL;
    }

    char buf[17];
    snprintf(buf, sizeof(buf), "%016" PRIx64, hash);
    return std::string(buf);
}

void ToolLoopDetector::recordCall(const std::string &toolName, const std::string &argsHash)
{
    HistoryEntry entry;
    entry.toolName = toolName;
    entry.argsHash = argsHash;
    entry.signature = hashString(toolName + ":" + argsHash);

    signatureCounts[entry.signature]++;

    history.push_back(std::move(entry));

    // evict oldest entry and decrement its signature count to keep counts in sync with window
    if (history.size() > static_cast<size_t>(loopConfig.historySize))
    {
        auto &evicted = history.front();
        auto it = signatureCounts.find(evicted.signature);

        if (it != signatureCounts.end())
        {
            it->second--;

            if (it->second <= 0)
            {
                signatureCounts.erase(it);
            }
        }

        history.pop_front();
    }
}

void ToolLoopDetector::recordResult(const std::string &toolName, const std::string &resultHash)
{
    // update the most recent entry matching this tool
    for (auto it = history.rbegin(); it != history.rend(); ++it)
    {
        if (it->toolName == toolName && it->resultHash.empty())
        {
            it->resultHash = resultHash;
            break;
        }
    }
}

LoopDetectionResult ToolLoopDetector::check() const
{
    // check in order of severity: circuit breaker > ping-pong > poll > generic

    LoopDetectionResult generic;

    if (loopConfig.enableGenericRepeat)
    {
        generic = checkGenericRepeat();

        if (generic.severity == LoopSeverity::CircuitBreaker)
        {
            return generic;
        }
    }

    if (loopConfig.enablePingPong)
    {
        auto pingPong = checkPingPong();

        if (pingPong.shouldStop)
        {
            return pingPong;
        }
    }

    if (loopConfig.enablePollNoProgress)
    {
        auto poll = checkKnownPollNoProgress();

        if (poll.shouldStop)
        {
            return poll;
        }
    }

    // global no-progress: any tool producing identical results
    {
        auto globalNp = checkGlobalNoProgress();

        if (globalNp.shouldStop)
        {
            return globalNp;
        }
    }

    if (generic.shouldStop)
    {
        return generic;
    }

    if (generic.severity != LoopSeverity::None)
    {
        return generic;
    }

    return {};
}

bool ToolLoopDetector::shouldEmitWarning(const std::string &warningKey, int count)
{
    int bucket = count / WARNING_BUCKET_SIZE;
    auto it = warningBuckets.find(warningKey);
    int lastBucket = (it != warningBuckets.end()) ? it->second : -1;

    if (bucket <= lastBucket)
    {
        return false;
    }

    warningBuckets[warningKey] = bucket;

    // evict oldest key if map is too large
    if (warningBuckets.size() > static_cast<size_t>(MAX_WARNING_KEYS))
    {
        warningBuckets.erase(warningBuckets.begin());
    }

    return true;
}

void ToolLoopDetector::reset()
{
    history.clear();
    signatureCounts.clear();
    warningBuckets.clear();
}

LoopDetectionResult ToolLoopDetector::checkGenericRepeat() const
{
    if (history.empty())
    {
        return {};
    }

    auto &latest = history.back();
    auto it = signatureCounts.find(latest.signature);

    if (it == signatureCounts.end())
    {
        return {};
    }

    int count = it->second;

    if (count >= loopConfig.circuitBreakerThreshold)
    {
        return {
            LoopSeverity::CircuitBreaker,
            "generic_repeat",
            "Tool '" + latest.toolName + "' called " + std::to_string(count) + " times with identical arguments (circuit breaker)",
            true,
        };
    }

    if (count >= loopConfig.criticalThreshold)
    {
        return {
            LoopSeverity::Critical,
            "generic_repeat",
            "Tool '" + latest.toolName + "' called " + std::to_string(count) + " times with identical arguments",
            true,
        };
    }

    if (count >= loopConfig.warningThreshold)
    {
        return {
            LoopSeverity::Warning,
            "generic_repeat",
            "Tool '" + latest.toolName + "' called " + std::to_string(count) + " times with identical arguments",
            false,
        };
    }

    return {};
}

LoopDetectionResult ToolLoopDetector::checkKnownPollNoProgress() const
{
    if (history.size() < 3)
    {
        return {};
    }

    auto &latest = history.back();

    if (KNOWN_POLL_TOOLS.find(latest.toolName) == KNOWN_POLL_TOOLS.end())
    {
        return {};
    }

    if (latest.resultHash.empty())
    {
        return {};
    }

    // count consecutive calls with same tool+args+result from the end
    int streak = 0;
    auto refSig = latest.signature;
    auto refResult = latest.resultHash;

    for (auto it = history.rbegin(); it != history.rend(); ++it)
    {
        if (it->signature == refSig && it->resultHash == refResult)
        {
            streak++;
        }
        else
        {
            break;
        }
    }

    if (streak >= loopConfig.criticalThreshold)
    {
        return {
            LoopSeverity::Critical,
            "known_poll_no_progress",
            "Tool '" + latest.toolName + "' polled " + std::to_string(streak) + " times with identical results (no progress)",
            true,
        };
    }

    if (streak >= loopConfig.warningThreshold)
    {
        return {
            LoopSeverity::Warning,
            "known_poll_no_progress",
            "Tool '" + latest.toolName + "' polled " + std::to_string(streak) + " times with identical results",
            false,
        };
    }

    return {};
}

LoopDetectionResult ToolLoopDetector::checkPingPong() const
{
    if (history.size() < 6)
    {
        return {};
    }

    // detect alternating A-B-A-B pattern from the end
    auto sz = static_cast<int>(history.size());

    if (sz < 4)
    {
        return {};
    }

    auto sigA = history[sz - 1].signature;
    auto sigB = history[sz - 2].signature;

    if (sigA == sigB)
    {
        return {};
    }

    int alternating = 0;
    bool noProgress = true;

    auto resultA = history[sz - 1].resultHash;
    auto resultB = history[sz - 2].resultHash;

    for (int i = sz - 1; i >= 0; i -= 2)
    {
        if (i - 1 < 0)
        {
            break;
        }

        if (history[i].signature != sigA || history[i - 1].signature != sigB)
        {
            break;
        }

        // check if results are stable (no progress)
        if (history[i].resultHash != resultA || history[i - 1].resultHash != resultB)
        {
            noProgress = false;
        }

        alternating += 2;
    }

    if (alternating >= loopConfig.criticalThreshold && noProgress)
    {
        return {
            LoopSeverity::Critical,
            "ping_pong",
            "Alternating pattern detected between '" + history[sz - 1].toolName + "' and '" + history[sz - 2].toolName + "' (" + std::to_string(alternating) + " calls, no progress)",
            true,
        };
    }

    return {};
}

LoopDetectionResult ToolLoopDetector::checkGlobalNoProgress() const
{
    if (history.size() < 3)
    {
        return {};
    }

    // count consecutive identical result hashes from the end (any tool)
    auto &latest = history.back();

    if (latest.resultHash.empty())
    {
        return {};
    }

    int streak = 0;

    for (auto it = history.rbegin(); it != history.rend(); ++it)
    {
        if (it->resultHash == latest.resultHash)
        {
            streak++;
        }
        else
        {
            break;
        }
    }

    if (streak >= loopConfig.circuitBreakerThreshold)
    {
        return {
            LoopSeverity::CircuitBreaker,
            "global_no_progress",
            "Global circuit breaker: " + std::to_string(streak) + " consecutive calls with identical results (no progress)",
            true,
        };
    }

    return {};
}

} // namespace agent
} // namespace ionclaw
