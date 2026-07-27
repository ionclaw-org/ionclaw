#include "ionclaw/util/FileHelper.hpp"

#include <filesystem>
#include <fstream>

#include "ionclaw/util/UniqueId.hpp"

namespace ionclaw
{
namespace util
{

namespace fs = std::filesystem;

std::string FileHelper::atomicWrite(const std::string &path, const std::string &content)
{
    fs::path target(path);
    auto dir = target.parent_path();
    std::error_code ec;

    if (!dir.empty())
    {
        fs::create_directories(dir, ec);
    }

    // a unique temp name in the same directory keeps the final rename atomic on the same filesystem and collision-free across threads
    auto tempPath = dir / (target.filename().string() + "." + ionclaw::util::UniqueId::shortId() + ".tmp");

    {
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);

        if (!file.is_open())
        {
            return "failed to open temp file for " + path;
        }

        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        file.flush();

        if (!file.good())
        {
            file.close();
            fs::remove(tempPath, ec);
            return "failed to write temp file for " + path;
        }
    }

    std::error_code renameEc;
    fs::rename(tempPath, target, renameEc);

    if (renameEc)
    {
        fs::remove(tempPath, ec);
        return "failed to finalize write for " + path + ": " + renameEc.message();
    }

    return "";
}

} // namespace util
} // namespace ionclaw
