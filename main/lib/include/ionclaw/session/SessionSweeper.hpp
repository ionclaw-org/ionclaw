#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ionclaw
{
namespace session
{

class SessionSweeper
{
public:
    SessionSweeper(const std::string &sessionsDir, int64_t maxDiskBytes, double highWaterRatio);

    // sweep oldest session files to reclaim disk space
    // excludeFilenames: filenames (not full paths) of sessions currently in cache; these are skipped
    void sweepIfNeeded(const std::vector<std::string> &excludeFilenames = {});

private:
    std::string sessionsDir;
    int64_t maxDiskBytes;
    int64_t highWaterBytes;

    int64_t measureTotalSize() const;
};

} // namespace session
} // namespace ionclaw
