#include "ionclaw/session/SessionSweeper.hpp"

#include <algorithm>
#include <filesystem>
#include <set>
#include <vector>

#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace session
{

namespace fs = std::filesystem;

SessionSweeper::SessionSweeper(const std::string &sessionsDir, int64_t maxDiskBytes, double highWaterRatio)
    : sessionsDir(sessionsDir)
    , maxDiskBytes(maxDiskBytes)
    , highWaterBytes(static_cast<int64_t>(maxDiskBytes * highWaterRatio))
{
}

int64_t SessionSweeper::measureTotalSize() const
{
    int64_t total = 0;
    std::error_code ec;

    for (const auto &entry : fs::directory_iterator(sessionsDir, ec))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".jsonl")
        {
            std::error_code sizeEc;
            auto size = entry.file_size(sizeEc);

            if (!sizeEc)
            {
                total += static_cast<int64_t>(size);
            }
        }
    }

    return total;
}

void SessionSweeper::sweepIfNeeded(const std::vector<std::string> &excludeFilenames)
{
    if (maxDiskBytes <= 0)
    {
        return;
    }

    auto total = measureTotalSize();

    if (total <= highWaterBytes)
    {
        return;
    }

    spdlog::info("[SessionSweeper] Disk usage {} bytes exceeds high water mark {} bytes, sweeping...",
                 total, highWaterBytes);

    // build set of filenames to skip (sessions currently in cache)
    std::set<std::string> excluded(excludeFilenames.begin(), excludeFilenames.end());

    // collect session files sorted by modification time (oldest first)
    struct FileEntry
    {
        fs::path path;
        int64_t size;
        fs::file_time_type modTime;
    };

    std::vector<FileEntry> files;
    std::error_code ec;

    for (const auto &entry : fs::directory_iterator(sessionsDir, ec))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".jsonl")
        {
            // skip files for sessions currently active in cache
            if (excluded.count(entry.path().filename().string()) > 0)
            {
                continue;
            }

            std::error_code sizeEc;
            std::error_code timeEc;
            auto size = entry.file_size(sizeEc);
            auto modTime = entry.last_write_time(timeEc);

            if (!sizeEc && !timeEc)
            {
                files.push_back({entry.path(), static_cast<int64_t>(size), modTime});
            }
        }
    }

    std::sort(files.begin(), files.end(), [](const FileEntry &a, const FileEntry &b)
              { return a.modTime < b.modTime; });

    int removed = 0;

    for (const auto &file : files)
    {
        if (total <= highWaterBytes)
        {
            break;
        }

        std::error_code removeEc;
        fs::remove(file.path, removeEc);

        if (!removeEc)
        {
            total -= file.size;
            removed++;
        }
    }

    spdlog::info("[SessionSweeper] Removed {} session files, new total {} bytes", removed, total);
}

} // namespace session
} // namespace ionclaw
