#include "ionclaw/channel/TelegramRunner.hpp"

#include "ionclaw/util/HttpClient.hpp"
#include "ionclaw/util/StringHelper.hpp"
#include "ionclaw/util/TimeHelper.hpp"
#include "spdlog/spdlog.h"
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace ionclaw
{
namespace channel
{

// telegram api base url
const char *TelegramRunner::TELEGRAM_API = "https://api.telegram.org";

// telegram api get request
std::string TelegramRunner::telegramGet(const std::string &token, const std::string &path, const std::string &proxy, int timeoutSeconds)
{
    std::string url = std::string(TELEGRAM_API) + "/bot" + token + path;
    auto resp = ionclaw::util::HttpClient::request(
        "GET", url, {}, "", timeoutSeconds, true, nullptr, proxy);

    if (resp.statusCode < 200 || resp.statusCode >= 300)
    {
        throw std::runtime_error("Telegram API GET " + path + " failed: " + std::to_string(resp.statusCode) + " " + resp.body);
    }

    return resp.body;
}

// telegram api post request
std::string TelegramRunner::telegramPost(const std::string &token, const std::string &path, const std::string &body, const std::string &proxy)
{
    std::string url = std::string(TELEGRAM_API) + "/bot" + token + path;
    auto resp = ionclaw::util::HttpClient::request(
        "POST", url, {{"Content-Type", "application/json"}}, body, 30, true, nullptr, proxy);

    if (resp.statusCode < 200 || resp.statusCode >= 300)
    {
        throw std::runtime_error("Telegram API POST " + path + " failed: " + std::to_string(resp.statusCode) + " " + resp.body);
    }

    return resp.body;
}

TelegramRunner::TelegramRunner(
    std::shared_ptr<ionclaw::bus::MessageBus> bus,
    std::shared_ptr<ionclaw::session::SessionManager> sessionManager,
    std::shared_ptr<ionclaw::task::TaskManager> taskManager,
    std::shared_ptr<ionclaw::bus::EventDispatcher> dispatcher,
    std::string token,
    std::vector<std::string> allowedUsers,
    std::string proxy,
    bool replyToMessage,
    std::string publicDir)
    : bus(std::move(bus))
    , sessionManager(std::move(sessionManager))
    , taskManager(std::move(taskManager))
    , dispatcher(std::move(dispatcher))
    , token(std::move(token))
    , allowedUsers(std::move(allowedUsers))
    , proxy(std::move(proxy))
    , replyToMessage(replyToMessage)
    , publicDir(std::move(publicDir))
{
}

void TelegramRunner::start()
{
    if (running.exchange(true))
    {
        return;
    }

    // clear any stale webhook before starting polling
    try
    {
        telegramPost(token, "/deleteWebhook", R"({"drop_pending_updates":false})", proxy);
        spdlog::info("[Telegram] Webhook cleared");
    }
    catch (const std::exception &e)
    {
        spdlog::warn("[Telegram] deleteWebhook failed: {}", e.what());
    }

    // validate bot token on startup
    try
    {
        auto body = telegramGet(token, "/getMe", proxy);
        auto j = nlohmann::json::parse(body);
        if (j.value("ok", false) && j.contains("result"))
        {
            auto botName = j["result"].value("username", "unknown");
            auto botId = j["result"].value("id", int64_t(0));
            spdlog::info("[Telegram] Bot authenticated: @{} (id={})", botName, botId);
        }
    }
    catch (const std::exception &e)
    {
        spdlog::error("[Telegram] Bot token validation failed: {}", e.what());
    }

    pollThread = std::thread(&TelegramRunner::pollLoop, this);
    outboundThread = std::thread(&TelegramRunner::outboundLoop, this);
    spdlog::info("[Telegram] Runner started");
}

void TelegramRunner::stop()
{
    if (!running.exchange(false))
    {
        return;
    }
    if (pollThread.joinable())
    {
        pollThread.join();
    }
    if (outboundThread.joinable())
    {
        outboundThread.join();
    }
    spdlog::info("[Telegram] Runner stopped");
}

bool TelegramRunner::isAllowed(const std::string &userId, const std::string &username) const
{
    if (allowedUsers.empty())
    {
        return true;
    }
    for (const auto &allowed : allowedUsers)
    {
        if (allowed == userId || allowed == username)
        {
            return true;
        }
    }
    return false;
}

// date-organized path: YYYY/MM/DD
std::string TelegramRunner::mediaDatePath() const
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = {};
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d/%02d/%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return buf;
}

// download a telegram file by file_id, save to public/media/, return relative path
std::string TelegramRunner::downloadTelegramFile(const std::string &fileId)
{
    // step 1: call getFile to get the file_path
    auto body = telegramGet(token, "/getFile?file_id=" + fileId, proxy);
    auto j = nlohmann::json::parse(body);

    if (!j.value("ok", false) || !j.contains("result") || !j["result"].contains("file_path"))
    {
        throw std::runtime_error("getFile failed for file_id: " + fileId);
    }

    std::string filePath = j["result"]["file_path"].get<std::string>();
    int64_t fileSize = j["result"].value("file_size", int64_t(0));

    // telegram bot api limit: 20MB for downloads
    if (fileSize > 20 * 1024 * 1024)
    {
        throw std::runtime_error("File too large for Telegram Bot API download: " + std::to_string(fileSize) + " bytes");
    }

    // step 2: download the file from telegram CDN
    std::string downloadUrl = std::string(TELEGRAM_API) + "/file/bot" + token + "/" + filePath;
    auto resp = ionclaw::util::HttpClient::request(
        "GET", downloadUrl, {}, "", 60, true, nullptr, proxy);

    if (resp.statusCode < 200 || resp.statusCode >= 300)
    {
        throw std::runtime_error("File download failed: " + std::to_string(resp.statusCode));
    }

    // step 3: save to public/media/YYYY/MM/DD/
    auto datePath = mediaDatePath();
    auto mediaDir = publicDir + "/media/" + datePath;
    std::error_code ec;
    fs::create_directories(mediaDir, ec);

    // preserve original extension from file_path
    auto ext = fs::path(filePath).extension().string();
    if (ext.empty())
    {
        ext = ".bin";
    }

    // generate unique filename
    auto id = ionclaw::util::TimeHelper::now();
    // strip non-alnum for filename safety
    std::string safeId;
    for (char c : id)
    {
        if (std::isalnum(static_cast<unsigned char>(c)))
        {
            safeId += c;
        }
    }
    auto filename = safeId + ext;
    auto fullPath = mediaDir + "/" + filename;

    std::ofstream out(fullPath, std::ios::binary);
    if (!out.is_open())
    {
        throw std::runtime_error("Failed to write media file: " + fullPath);
    }
    out.write(resp.body.data(), static_cast<std::streamsize>(resp.body.size()));
    out.flush();
    if (out.fail())
    {
        throw std::runtime_error("Write failed (disk full or I/O error): " + fullPath);
    }
    out.close();

    // return relative path (same format as web upload)
    return "public/media/" + datePath + "/" + filename;
}

void TelegramRunner::sendTypingAction(const std::string &chatId)
{
    try
    {
        nlohmann::json body;
        body["chat_id"] = chatId;
        body["action"] = "typing";
        telegramPost(token, "/sendChatAction", body.dump(), proxy);
    }
    catch (const std::exception &e)
    {
        spdlog::debug("[Telegram] sendChatAction failed: {}", e.what());
    }
}

// escape a string for Telegram HTML
static std::string escapeHtml(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
        switch (c)
        {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        default:
            out += c;
        }
    }
    return out;
}

// find closing marker starting from pos, returns npos if not found
static size_t findClosing(const std::string &s, size_t pos, const std::string &marker)
{
    auto found = s.find(marker, pos);
    // must not be empty content
    if (found != std::string::npos && found > pos)
    {
        return found;
    }
    return std::string::npos;
}

std::string TelegramRunner::markdownToTelegramHtml(const std::string &md)
{
    std::string out;
    out.reserve(md.size() + md.size() / 4);

    size_t i = 0;
    size_t len = md.size();
    bool inCodeBlock = false;

    while (i < len)
    {
        // fenced code block: ```
        if (!inCodeBlock && i + 2 < len && md[i] == '`' && md[i + 1] == '`' && md[i + 2] == '`')
        {
            // skip opening ``` and optional language tag
            size_t start = i + 3;
            // skip language identifier until newline
            while (start < len && md[start] != '\n')
            {
                ++start;
            }
            if (start < len)
            {
                ++start; // skip the newline
            }
            // find closing ```
            auto end = md.find("```", start);
            if (end != std::string::npos)
            {
                out += "<pre><code>";
                out += escapeHtml(md.substr(start, end - start));
                out += "</code></pre>";
                i = end + 3;
                continue;
            }
            // no closing found, output literally
            out += escapeHtml("```");
            i += 3;
            continue;
        }

        // inline code: `text`
        if (md[i] == '`')
        {
            auto end = findClosing(md, i + 1, "`");
            if (end != std::string::npos)
            {
                out += "<code>";
                out += escapeHtml(md.substr(i + 1, end - i - 1));
                out += "</code>";
                i = end + 1;
                continue;
            }
        }

        // bold: **text**
        if (i + 1 < len && md[i] == '*' && md[i + 1] == '*')
        {
            auto end = findClosing(md, i + 2, "**");
            if (end != std::string::npos)
            {
                out += "<b>";
                out += escapeHtml(md.substr(i + 2, end - i - 2));
                out += "</b>";
                i = end + 2;
                continue;
            }
        }

        // strikethrough: ~~text~~
        if (i + 1 < len && md[i] == '~' && md[i + 1] == '~')
        {
            auto end = findClosing(md, i + 2, "~~");
            if (end != std::string::npos)
            {
                out += "<s>";
                out += escapeHtml(md.substr(i + 2, end - i - 2));
                out += "</s>";
                i = end + 2;
                continue;
            }
        }

        // italic: *text* (single asterisk, not double)
        if (md[i] == '*' && (i + 1 >= len || md[i + 1] != '*'))
        {
            auto end = md.find('*', i + 1);
            if (end != std::string::npos && end > i + 1)
            {
                out += "<i>";
                out += escapeHtml(md.substr(i + 1, end - i - 1));
                out += "</i>";
                i = end + 1;
                continue;
            }
        }

        // link: [text](url)
        if (md[i] == '[')
        {
            auto closeBracket = md.find(']', i + 1);
            if (closeBracket != std::string::npos && closeBracket + 1 < len && md[closeBracket + 1] == '(')
            {
                auto closeParen = md.find(')', closeBracket + 2);
                if (closeParen != std::string::npos)
                {
                    auto text = md.substr(i + 1, closeBracket - i - 1);
                    auto url = md.substr(closeBracket + 2, closeParen - closeBracket - 2);
                    out += "<a href=\"";
                    out += escapeHtml(url);
                    out += "\">";
                    out += escapeHtml(text);
                    out += "</a>";
                    i = closeParen + 1;
                    continue;
                }
            }
        }

        // header at line start: # text → bold
        if (md[i] == '#' && (i == 0 || md[i - 1] == '\n'))
        {
            size_t hEnd = i;
            while (hEnd < len && md[hEnd] == '#')
            {
                ++hEnd;
            }
            if (hEnd < len && md[hEnd] == ' ')
            {
                ++hEnd; // skip space after #
                auto lineEnd = md.find('\n', hEnd);
                if (lineEnd == std::string::npos)
                {
                    lineEnd = len;
                }
                out += "<b>";
                out += escapeHtml(md.substr(hEnd, lineEnd - hEnd));
                out += "</b>";
                i = lineEnd;
                continue;
            }
        }

        // default: escape and output
        switch (md[i])
        {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        default:
            out += md[i];
        }
        ++i;
    }

    return out;
}

void TelegramRunner::sendTextMessage(const std::string &chatId, const std::string &text, int replyToMessageId)
{
    auto html = markdownToTelegramHtml(text);

    nlohmann::json body;
    body["chat_id"] = chatId; // send as string — Telegram accepts both
    body["text"] = html;
    body["parse_mode"] = "HTML";
    if (replyToMessageId != 0)
    {
        body["reply_to_message_id"] = replyToMessageId;
    }

    try
    {
        telegramPost(token, "/sendMessage", body.dump(), proxy);
    }
    catch (const std::exception &firstErr)
    {
        std::string err = firstErr.what();
        spdlog::error("[Telegram] sendMessage failed for chatId={}: {}", chatId, err);

        // chat not found = user never sent /start to the bot
        if (err.find("chat not found") != std::string::npos)
        {
            return;
        }

        // fallback: send as plain text if HTML parsing fails
        spdlog::info("[Telegram] Retrying as plain text");
        body.erase("parse_mode");
        body["text"] = text;
        try
        {
            telegramPost(token, "/sendMessage", body.dump(), proxy);
        }
        catch (const std::exception &retryErr)
        {
            spdlog::error("[Telegram] Plain text retry also failed: {}", retryErr.what());
        }
    }
}

void TelegramRunner::sendChunkedMessage(const std::string &chatId, const std::string &text, int replyToMessageId)
{
    if (text.size() <= MAX_MESSAGE_LENGTH)
    {
        sendTextMessage(chatId, text, replyToMessageId);
        return;
    }

    // split at line boundaries within the limit
    size_t offset = 0;
    bool firstChunk = true;

    while (offset < text.size())
    {
        size_t remaining = text.size() - offset;
        size_t chunkSize = std::min(remaining, MAX_MESSAGE_LENGTH);

        if (chunkSize < remaining)
        {
            // try to break at last newline within chunk
            auto lastNewline = text.rfind('\n', offset + chunkSize);
            if (lastNewline != std::string::npos && lastNewline > offset)
            {
                chunkSize = lastNewline - offset + 1;
            }
        }

        auto chunk = text.substr(offset, chunkSize);
        int replyId = firstChunk ? replyToMessageId : 0;
        sendTextMessage(chatId, chunk, replyId);

        offset += chunkSize;
        firstChunk = false;
    }
}

void TelegramRunner::processUpdate(const nlohmann::json &update)
{
    int64_t updateId = update.value("update_id", 0);
    if (updateId > lastUpdateId)
    {
        lastUpdateId = updateId;
    }

    if (!update.contains("message"))
    {
        return;
    }

    const auto &msg = update["message"];
    if (!msg.contains("from") || !msg.contains("chat"))
    {
        return;
    }

    // determine content: text, caption, or media-only
    std::string text;
    if (msg.contains("text"))
    {
        text = msg["text"].get<std::string>();
    }
    else if (msg.contains("caption"))
    {
        text = msg["caption"].get<std::string>();
    }

    // collect media files
    std::vector<std::string> mediaFiles;

    // photo: array of sizes, take the last (highest resolution)
    if (msg.contains("photo") && msg["photo"].is_array() && !msg["photo"].empty())
    {
        auto &photos = msg["photo"];
        auto &largest = photos.back();
        if (largest.contains("file_id"))
        {
            try
            {
                auto path = downloadTelegramFile(largest["file_id"].get<std::string>());
                mediaFiles.push_back(path);
                spdlog::info("[Telegram] Downloaded photo: {}", path);
            }
            catch (const std::exception &e)
            {
                spdlog::error("[Telegram] Photo download failed: {}", e.what());
            }
        }
    }

    // voice message
    if (msg.contains("voice") && msg["voice"].contains("file_id"))
    {
        try
        {
            auto path = downloadTelegramFile(msg["voice"]["file_id"].get<std::string>());
            mediaFiles.push_back(path);
            spdlog::info("[Telegram] Downloaded voice: {}", path);
        }
        catch (const std::exception &e)
        {
            spdlog::error("[Telegram] Voice download failed: {}", e.what());
        }
    }

    // audio file
    if (msg.contains("audio") && msg["audio"].contains("file_id"))
    {
        try
        {
            auto path = downloadTelegramFile(msg["audio"]["file_id"].get<std::string>());
            mediaFiles.push_back(path);
            spdlog::info("[Telegram] Downloaded audio: {}", path);
        }
        catch (const std::exception &e)
        {
            spdlog::error("[Telegram] Audio download failed: {}", e.what());
        }
    }

    // document (generic file)
    if (msg.contains("document") && msg["document"].contains("file_id"))
    {
        try
        {
            auto path = downloadTelegramFile(msg["document"]["file_id"].get<std::string>());
            mediaFiles.push_back(path);
            spdlog::info("[Telegram] Downloaded document: {}", path);
        }
        catch (const std::exception &e)
        {
            spdlog::error("[Telegram] Document download failed: {}", e.what());
        }
    }

    // skip if no text and no media
    if (text.empty() && mediaFiles.empty())
    {
        return;
    }

    const auto &from = msg["from"];
    int64_t fromId = from.value("id", int64_t(0));
    std::string username = from.value("username", "");
    int64_t chatId = msg["chat"].value("id", int64_t(0));
    int messageId = msg.value("message_id", 0);

    std::string userIdStr = std::to_string(fromId);
    if (!isAllowed(userIdStr, username))
    {
        spdlog::debug("[Telegram] Ignoring message from non-allowed user: {}", userIdStr);
        return;
    }

    std::string chatIdStr = std::to_string(chatId);
    std::string sessionKey = "telegram:" + chatIdStr;

    if (!taskManager || !sessionManager || !dispatcher)
    {
        spdlog::error("[Telegram] Required services not initialized");
        return;
    }

    // send typing indicator
    sendTypingAction(chatIdStr);

    // build task title
    std::string taskTitle;
    if (!text.empty())
    {
        taskTitle = ionclaw::util::StringHelper::utf8SafeTruncate(text, 100);
    }
    else if (!mediaFiles.empty())
    {
        taskTitle = "[media]";
    }

    auto task = taskManager->createTask(taskTitle, text, "telegram", chatIdStr);
    sessionManager->ensureSession(sessionKey);

    ionclaw::session::SessionMessage userMsg;
    userMsg.role = "user";
    userMsg.content = text;
    userMsg.timestamp = ionclaw::util::TimeHelper::now();
    if (replyToMessage && messageId != 0)
    {
        userMsg.raw["reply_to_message_id"] = messageId;
    }
    // persist media references in session (same format as web: plain path strings)
    for (const auto &mediaPath : mediaFiles)
    {
        userMsg.media.push_back(mediaPath);
    }
    sessionManager->addMessage(sessionKey, userMsg);
    dispatcher->broadcast("sessions:updated", nlohmann::json::object());

    // notify web client about the new user message (real-time update with media)
    {
        nlohmann::json userMsgEvent;
        userMsgEvent["chat_id"] = sessionKey;
        userMsgEvent["content"] = text;
        userMsgEvent["channel"] = "telegram";
        if (!mediaFiles.empty())
        {
            userMsgEvent["media"] = nlohmann::json(mediaFiles);
        }
        dispatcher->broadcast("chat:user_message", userMsgEvent);
    }

    ionclaw::bus::InboundMessage inbound;
    inbound.channel = "telegram";
    inbound.senderId = userIdStr;
    inbound.chatId = chatIdStr;
    inbound.content = text;
    inbound.media = mediaFiles;
    inbound.metadata = {{"task_id", task.id}, {"message_saved", true}};
    if (replyToMessage && messageId != 0)
    {
        inbound.metadata["reply_to_message_id"] = messageId;
    }
    bus->publishInbound(inbound);
}

void TelegramRunner::pollLoop()
{
    while (running.load())
    {
        try
        {
            std::string path = "/getUpdates?timeout=" + std::to_string(POLL_TIMEOUT_SEC);
            if (lastUpdateId > 0)
            {
                path += "&offset=" + std::to_string(lastUpdateId + 1);
            }
            std::string body = telegramGet(token, path, proxy, POLL_TIMEOUT_SEC + 2);
            auto j = nlohmann::json::parse(body);
            if (!j.value("ok", false) || !j.contains("result"))
            {
                continue;
            }
            for (const auto &update : j["result"])
            {
                processUpdate(update);
            }
        }
        catch (const std::exception &e)
        {
            if (running.load())
            {
                spdlog::warn("[Telegram] Poll error: {}", e.what());
            }
        }
    }
}

void TelegramRunner::outboundLoop()
{
    while (running.load())
    {
        ionclaw::bus::OutboundMessage outbound;
        if (!bus->consumeOutbound(outbound, OUTBOUND_POLL_MS))
        {
            continue;
        }
        if (outbound.channel != "telegram")
        {
            spdlog::debug("[Telegram] Skipping outbound for channel={}", outbound.channel);
            continue;
        }
        try
        {
            std::string chatId = outbound.chatId;
            auto colon = chatId.find(':');
            if (colon != std::string::npos)
            {
                chatId = chatId.substr(colon + 1);
            }

            spdlog::debug("[Telegram] Sending outbound: chatId={}, contentLen={}", chatId, outbound.content.size());

            if (chatId.empty() || outbound.content.empty())
            {
                spdlog::warn("[Telegram] Skipping empty outbound (chatId={}, contentLen={})", chatId, outbound.content.size());
                continue;
            }

            int replyToMessageId = 0;
            if (outbound.metadata.contains("reply_to_message_id") && outbound.metadata["reply_to_message_id"].is_number_integer())
            {
                replyToMessageId = outbound.metadata["reply_to_message_id"].get<int>();
            }

            sendChunkedMessage(chatId, outbound.content, replyToMessageId);
        }
        catch (const std::exception &e)
        {
            spdlog::error("[Telegram] Send failed: {}", e.what());
        }
    }
}

} // namespace channel
} // namespace ionclaw
