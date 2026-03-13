#include "ionclaw/server/Routes.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "Poco/Net/HTMLForm.h"
#include "Poco/Net/PartHandler.h"

#include "ionclaw/util/StringHelper.hpp"
#include "spdlog/spdlog.h"

#include "ionclaw/bus/Events.hpp"
#include "ionclaw/util/TimeHelper.hpp"
#include "ionclaw/util/UniqueId.hpp"

namespace ionclaw
{
namespace server
{

namespace fs = std::filesystem;

// build a date-organized media directory path: public/media/YYYY/MM/DD
std::string Routes::mediaDatePath()
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif

    std::ostringstream oss;
    oss << std::setfill('0')
        << (tm.tm_year + 1900) << "/"
        << std::setw(2) << (tm.tm_mon + 1) << "/"
        << std::setw(2) << tm.tm_mday;

    return oss.str();
}

void Routes::handleChatSend(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp)
{
    try
    {
        auto body = nlohmann::json::parse(readBody(req));
        auto message = body.value("message", "");
        auto sessionId = body.value("session_id", std::string("direct"));

        // extract media paths from request
        std::vector<std::string> media;

        if (body.contains("media") && body["media"].is_array())
        {
            for (const auto &item : body["media"])
            {
                if (item.is_string())
                {
                    media.push_back(item.get<std::string>());
                }
            }
        }

        // require at least a message or media attachment
        if (message.empty() && media.empty())
        {
            sendError(resp, "Message or media is required");
            return;
        }

        // strip channel prefix from session_id to avoid double prefix
        // client sends "web:{uuid}", InboundMessage::sessionKey() will add "web:" back
        auto chatId = sessionId;
        auto colonPos = chatId.find(':');

        if (colonPos != std::string::npos)
        {
            chatId = chatId.substr(colonPos + 1);
        }

        // create task for tracking
        auto taskTitle = message.empty() ? "[media]" : ionclaw::util::StringHelper::utf8SafeTruncate(message, 100);

        auto task = taskManager->createTask(
            taskTitle,
            message,
            "web",
            chatId);

        // persist the user message immediately so the session exists on disk
        // before the async agent loop picks it up (page refresh always shows it)
        auto sessionKey = "web:" + chatId;
        sessionManager->ensureSession(sessionKey);

        ionclaw::session::SessionMessage userMsg;
        userMsg.role = "user";
        userMsg.content = message;
        userMsg.timestamp = ionclaw::util::TimeHelper::now();

        for (const auto &path : media)
        {
            userMsg.media.push_back(path);
        }

        sessionManager->addMessage(sessionKey, userMsg);
        dispatcher->broadcast("sessions:updated", nlohmann::json::object());

        // build inbound message
        ionclaw::bus::InboundMessage inbound;
        inbound.channel = "web";
        inbound.senderId = "web_user";
        inbound.chatId = chatId;
        inbound.content = message;
        inbound.media = media;
        inbound.metadata = {{"task_id", task.id}, {"message_saved", true}};

        // parse optional queue_mode from request
        if (body.contains("queue_mode") && body["queue_mode"].is_string())
        {
            inbound.queueMode = ionclaw::bus::normalizeQueueMode(body["queue_mode"].get<std::string>());
        }

        // steer bypass: if session has an active turn and mode is steer_compatible,
        // inject directly into SessionQueue (bypasses MessageBus which blocks during turns)
        if (inbound.queueMode.has_value() && orchestrator && config && orchestrator->isSessionActive(sessionKey))
        {
            auto mode = inbound.queueMode.value();

            if (mode == ionclaw::bus::QueueMode::Steer ||
                mode == ionclaw::bus::QueueMode::SteerBacklog)
            {
                auto *sq = orchestrator->getSessionQueue();

                if (sq)
                {
                    auto settings = ionclaw::bus::resolveQueueSettings(*config, "web", inbound.queueMode);
                    sq->enqueue(sessionKey, inbound, ionclaw::bus::QueueMode::Steer, settings);

                    // also enqueue followup backup for steer_backlog
                    if (mode == ionclaw::bus::QueueMode::SteerBacklog)
                    {
                        sq->enqueue(sessionKey, inbound, ionclaw::bus::QueueMode::Followup, settings);
                    }

                    sendJson(resp, {{"task_id", task.id}, {"session_id", sessionKey}, {"queued", "steer"}});
                    return;
                }
            }
        }

        // publish to message bus for async processing
        bus->publishInbound(inbound);

        sendJson(resp, {{"task_id", task.id}, {"session_id", "web:" + chatId}});
    }
    catch (const std::exception &e)
    {
        sendError(resp, e.what(), 500);
    }
}

void Routes::handleChatUpload(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp)
{
    try
    {
        // organize uploads by date: public/media/YYYY/MM/DD/
        auto datePath = mediaDatePath();
        auto mediaDir = publicDir + "/media/" + datePath;
        std::error_code ec;
        fs::create_directories(mediaDir, ec);

        std::string relativePrefix = "public/media/" + datePath + "/";
        nlohmann::json paths = nlohmann::json::array();

        // collect uploaded files via PartHandler
        class UploadHandler : public Poco::Net::PartHandler
        {
        public:
            UploadHandler(const std::string &dir, const std::string &relPrefix, nlohmann::json &paths)
                : dir(dir)
                , relPrefix(relPrefix)
                , paths(paths)
            {
            }

            void handlePart(const Poco::Net::MessageHeader &header, std::istream &stream) override
            {
                auto disp = header.get("Content-Disposition", "");
                auto namePos = disp.find("filename=\"");

                std::string ext = ".bin";

                if (namePos != std::string::npos)
                {
                    auto start = namePos + 10;
                    auto end = disp.find('"', start);

                    if (end != std::string::npos)
                    {
                        auto origName = disp.substr(start, end - start);
                        auto dotPos = origName.rfind('.');

                        if (dotPos != std::string::npos)
                        {
                            ext = origName.substr(dotPos);
                        }
                    }
                }

                auto uniqueName = ionclaw::util::UniqueId::shortId() + ext;
                auto fullPath = dir + "/" + uniqueName;

                std::ofstream ofs(fullPath, std::ios::binary);
                ofs << stream.rdbuf();
                ofs.flush();

                if (ofs.good())
                {
                    paths.push_back(relPrefix + uniqueName);
                }
                else
                {
                    spdlog::warn("[Routes] Failed to write upload file: {}", fullPath);
                }
            }

        private:
            const std::string &dir;
            const std::string &relPrefix;
            nlohmann::json &paths;
        };

        UploadHandler handler(mediaDir, relativePrefix, paths);
        Poco::Net::HTMLForm form(req, req.stream(), handler);

        sendJson(resp, {{"paths", paths}});
    }
    catch (const std::exception &e)
    {
        sendError(resp, e.what(), 500);
    }
}

void Routes::handleChatSessions(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp)
{
    auto sessions = sessionManager->listSessions();
    nlohmann::json result = nlohmann::json::array();

    for (const auto &info : sessions)
    {
        result.push_back({
            {"key", info.key},
            {"created_at", info.createdAt},
            {"updated_at", info.updatedAt},
            {"display_name", info.displayName},
            {"channel", info.channel},
        });
    }

    sendJson(resp, result);
}

void Routes::handleChatSession(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp, const std::string &sessionId)
{
    try
    {
        auto sessionKey = resolveSessionKey(sessionId);
        auto messages = sessionManager->getHistory(sessionKey);

        nlohmann::json msgArray = nlohmann::json::array();

        for (const auto &msg : messages)
        {
            msgArray.push_back(msg.toJson());
        }

        auto session = sessionManager->getOrCreate(sessionKey);

        sendJson(resp, {
                           {"key", sessionKey},
                           {"messages", msgArray},
                           {"live_state", session.liveState},
                           {"created_at", session.createdAt},
                           {"updated_at", session.updatedAt},
                       });
    }
    catch (const std::exception &e)
    {
        sendError(resp, e.what(), 404);
    }
}

void Routes::handleChatSessionDelete(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp, const std::string &sessionId)
{
    try
    {
        auto sessionKey = resolveSessionKey(sessionId);
        sessionManager->deleteSession(sessionKey);
        dispatcher->broadcast("sessions:updated", {});
        sendJson(resp, {{"status", "deleted"}});
    }
    catch (const std::exception &e)
    {
        sendError(resp, e.what(), 500);
    }
}

} // namespace server
} // namespace ionclaw
