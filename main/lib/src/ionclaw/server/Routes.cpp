#include "ionclaw/server/Routes.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <set>
#include <vector>

namespace ionclaw
{
namespace server
{

namespace fs = std::filesystem;

Routes::Routes(
    std::shared_ptr<ionclaw::config::Config> config,
    std::shared_ptr<Auth> auth,
    std::shared_ptr<ionclaw::agent::Orchestrator> orchestrator,
    std::shared_ptr<ionclaw::channel::ChannelManager> channelManager,
    std::shared_ptr<ionclaw::heartbeat::HeartbeatService> heartbeatService,
    std::shared_ptr<ionclaw::cron::CronService> cronService,
    std::shared_ptr<ionclaw::session::SessionManager> sessionManager,
    std::shared_ptr<ionclaw::task::TaskManager> taskManager,
    std::shared_ptr<ionclaw::bus::MessageBus> bus,
    std::shared_ptr<ionclaw::bus::EventDispatcher> dispatcher,
    std::shared_ptr<WebSocketManager> wsManager,
    const std::string &webDir,
    const std::string &projectRoot,
    const std::string &publicDir,
    const std::string &workspaceDir)
    : config(std::move(config))
    , auth(std::move(auth))
    , orchestrator(std::move(orchestrator))
    , channelManager(std::move(channelManager))
    , heartbeatService(std::move(heartbeatService))
    , cronService(std::move(cronService))
    , sessionManager(std::move(sessionManager))
    , taskManager(std::move(taskManager))
    , bus(std::move(bus))
    , dispatcher(std::move(dispatcher))
    , wsManager(std::move(wsManager))
    , webDir(webDir)
    , projectRoot(projectRoot)
    , publicDir(publicDir)
    , workspaceDir(workspaceDir)
{
}

// --- response helpers ---

void Routes::sendJson(Poco::Net::HTTPServerResponse &resp, const nlohmann::json &body, int status)
{
    resp.setStatus(static_cast<Poco::Net::HTTPResponse::HTTPStatus>(status));
    resp.setContentType("application/json");
    auto &out = resp.send();
    out << body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

void Routes::sendError(Poco::Net::HTTPServerResponse &resp, const std::string &message, int status)
{
    nlohmann::json body = {{"error", message}};
    sendJson(resp, body, status);
}

std::string Routes::readBody(Poco::Net::HTTPServerRequest &req)
{
    static constexpr size_t MAX_BODY_BYTES = 10 * 1024 * 1024; // 10 MB

    std::istream &is = req.stream();
    std::string body;

    auto contentLength = req.getContentLength();

    if (contentLength > 0 && static_cast<size_t>(contentLength) <= MAX_BODY_BYTES)
    {
        body.reserve(static_cast<size_t>(contentLength));
    }

    std::array<char, 8192> buf;

    while (is.read(buf.data(), buf.size()) || is.gcount() > 0)
    {
        body.append(buf.data(), static_cast<size_t>(is.gcount()));

        if (body.size() > MAX_BODY_BYTES)
        {
            throw std::runtime_error("Request body exceeds maximum allowed size");
        }
    }

    return body;
}

// prefixes session id with "web:" if no channel prefix present
std::string Routes::resolveSessionKey(const std::string &sessionId) const
{
    if (sessionId.find(':') == std::string::npos)
    {
        return "web:" + sessionId;
    }

    return sessionId;
}

std::string Routes::detectFileType(const std::string &ext) const
{
    if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif" || ext == "svg" || ext == "webp" || ext == "bmp" || ext == "ico")
    {
        return "image";
    }

    if (ext == "mp4" || ext == "webm" || ext == "avi" || ext == "mov" || ext == "mkv")
    {
        return "video";
    }

    if (ext == "mp3" || ext == "wav" || ext == "ogg" || ext == "flac" || ext == "m4a")
    {
        return "audio";
    }

    if (ext == "txt" || ext == "md" || ext == "json" || ext == "yml" || ext == "yaml" || ext == "xml" ||
        ext == "html" || ext == "css" || ext == "js" || ext == "ts" || ext == "py" || ext == "cpp" ||
        ext == "hpp" || ext == "h" || ext == "c" || ext == "java" || ext == "rs" || ext == "go" ||
        ext == "rb" || ext == "sh" || ext == "bat" || ext == "toml" || ext == "ini" || ext == "cfg" ||
        ext == "conf" || ext == "log" || ext == "csv" || ext == "sql" || ext == "dart" || ext == "swift" ||
        ext == "kt" || ext == "gradle" || ext == "cmake" || ext.empty())
    {
        return "text";
    }

    return "binary";
}

// directories to skip when building project file tree
const std::set<std::string> Routes::SKIP_DIR_NAMES = {
    "node_modules", "__pycache__", ".venv", ".idea", ".vscode", "dist", "build", ".next"};

// builds a recursive json file tree with paths relative to rootPath
nlohmann::json Routes::buildFileTree(const std::string &dirPath, const std::string &rootPath) const
{
    return buildFileTreeImpl(dirPath, rootPath, false);
}

// builds a project file tree, skipping common non-essential directories
nlohmann::json Routes::buildFileTreeFromProject(const std::string &dirPath, const std::string &rootPath) const
{
    return buildFileTreeImpl(dirPath, rootPath, true);
}

// unified file tree builder
nlohmann::json Routes::buildFileTreeImpl(const std::string &dirPath, const std::string &rootPath, bool skipNonEssential) const
{
    nlohmann::json result = nlohmann::json::array();

    if (!fs::exists(dirPath) || !fs::is_directory(dirPath))
    {
        return result;
    }

    std::vector<fs::directory_entry> entries;

    for (const auto &entry : fs::directory_iterator(dirPath))
    {
        entries.push_back(entry);
    }

    std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b)
              {
        if (a.is_directory() != b.is_directory())
        {
            return a.is_directory() > b.is_directory();
        }

        return a.path().filename().string() < b.path().filename().string(); });

    for (const auto &entry : entries)
    {
        auto name = entry.path().filename().string();

        if (isSystemFile(name) || isProtectedFile(name))
        {
            continue;
        }

        if (skipNonEssential && entry.is_directory() && SKIP_DIR_NAMES.count(name) != 0)
        {
            continue;
        }

        auto relativePath = fs::relative(entry.path(), rootPath).string();

        if (entry.is_directory())
        {
            result.push_back({
                {"name", name},
                {"path", relativePath},
                {"type", "directory"},
                {"children", buildFileTreeImpl(entry.path().string(), rootPath, skipNonEssential)},
            });
        }
        else if (entry.is_regular_file())
        {
            result.push_back({
                {"name", name},
                {"path", relativePath},
                {"type", "file"},
                {"size", static_cast<int64_t>(entry.file_size())},
            });
        }
    }

    return result;
}

} // namespace server
} // namespace ionclaw
