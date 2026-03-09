#pragma once

#include <set>
#include <string>
#include <vector>

#include "ionclaw/provider/LlmProvider.hpp"
#include "ionclaw/session/SessionManager.hpp"

namespace ionclaw
{
namespace agent
{

struct MemorySearchResult
{
    std::string file;
    int line = 0;
    std::string context;
    double score = 0.0;
};

class MemoryStore
{
public:
    explicit MemoryStore(const std::string &workspacePath);

    std::string getMemoryContext() const;
    std::string getHistoryContext(int maxLines = 0) const;

    std::vector<MemorySearchResult> searchMemory(const std::string &query) const;

    std::vector<ionclaw::provider::Message> getConsolidationMessages(
        const ionclaw::session::Session &session, int window) const;

    static void markConsolidated(ionclaw::session::Session &session, int count);

private:
    std::string memoryDir;

    static const std::set<std::string> STOP_WORDS;

    static std::vector<std::string> extractKeywords(const std::string &query);
    static double computeTemporalDecay(const std::string &filePath, const std::string &baseDir);

    static bool isStopWord(const std::string &token);
    static bool isCjkCodepoint(uint32_t cp);
    static std::vector<uint32_t> toCodepoints(const std::string &utf8);
    static std::string codepointsToUtf8(const std::vector<uint32_t> &cps);
};

} // namespace agent
} // namespace ionclaw
