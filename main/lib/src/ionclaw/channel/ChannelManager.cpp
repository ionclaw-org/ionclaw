#include "ionclaw/channel/ChannelManager.hpp"

#include "ionclaw/channel/TelegramRunner.hpp"
#include "ionclaw/channel/WhatsAppRunner.hpp"
#include "ionclaw/config/Config.hpp"
#include "ionclaw/mcp/McpDispatcher.hpp"
#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace channel
{

ChannelManager::ChannelManager(std::shared_ptr<ionclaw::config::ConfigStore> configStore, std::shared_ptr<ionclaw::bus::MessageBus> bus, std::shared_ptr<ionclaw::session::SessionManager> sessionManager, std::shared_ptr<ionclaw::task::TaskManager> taskManager, std::shared_ptr<ionclaw::bus::EventDispatcher> dispatcher, std::shared_ptr<ionclaw::mcp::McpDispatcher> mcpDispatcher)
    : configStore(std::move(configStore))
    , bus(std::move(bus))
    , sessionManager(std::move(sessionManager))
    , taskManager(std::move(taskManager))
    , dispatcher(std::move(dispatcher))
    , mcpDispatcher(std::move(mcpDispatcher))
{
}

ChannelManager::~ChannelManager() = default;

void ChannelManager::startChannel(const std::string &name)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (name == "telegram")
        startTelegram();
    else if (name == "whatsapp_zapi" || name == "whatsapp_meta")
        startWhatsApp();
    else if (name == "mcp")
        startMcp();
}

void ChannelManager::stopChannel(const std::string &name)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (name == "telegram")
        stopTelegram();
    else if (name == "whatsapp_zapi" || name == "whatsapp_meta")
    {
        // the two providers share one runner, so keep it alive while the other is still enabled
        auto config = configStore->snapshot();
        auto z = config->channels.find("whatsapp_zapi");
        auto m = config->channels.find("whatsapp_meta");
        bool anyEnabled = (z != config->channels.end() && z->second.enabled) || (m != config->channels.end() && m->second.enabled);

        if (!anyEnabled)
        {
            stopWhatsApp();
        }
    }
    else if (name == "mcp")
        stopMcp();
}

void ChannelManager::stopAll()
{
    std::unique_ptr<TelegramRunner> telegram;
    std::unique_ptr<WhatsAppRunner> whatsApp;

    {
        std::lock_guard<std::mutex> lock(mutex);
        telegram = std::move(telegramRunner);
        whatsApp = std::move(whatsAppRunner);
    }

    // join the runners outside the lock so a 32s telegram long-poll cannot hold the manager mutex for its whole window
    if (telegram)
    {
        telegram->stop();
    }

    if (whatsApp)
    {
        whatsApp->stop();
    }

    stopMcp();
}

void ChannelManager::startWhatsApp()
{
    // one runner serves both providers via the shared whatsapp outbound queue
    if (whatsAppRunner)
    {
        return;
    }

    // the runner holds an immutable snapshot and is recreated on channel start/stop, so it never races a config restart
    whatsAppRunner = std::make_unique<WhatsAppRunner>(configStore->snapshot(), bus);
    whatsAppRunner->start();
    spdlog::info("[ChannelManager] WhatsApp runner started");
}

void ChannelManager::stopWhatsApp()
{
    if (!whatsAppRunner)
    {
        return;
    }

    whatsAppRunner->stop();
    whatsAppRunner.reset();
    spdlog::info("[ChannelManager] WhatsApp runner stopped");
}

void ChannelManager::startTelegram()
{
    if (telegramRunner)
    {
        return;
    }

    auto config = configStore->snapshot();
    auto it = config->channels.find("telegram");

    if (it == config->channels.end())
    {
        throw std::runtime_error("[ChannelManager] Telegram channel not in config");
    }

    const auto &ch = it->second;

    if (ch.credential.empty())
    {
        throw std::runtime_error("[ChannelManager] Telegram credential not set");
    }

    auto credIt = config->credentials.find(ch.credential);

    if (credIt == config->credentials.end())
    {
        throw std::runtime_error("[ChannelManager] Telegram credential '" + ch.credential + "' not found");
    }

    const auto &cred = credIt->second;
    std::string token = cred.key.empty() ? cred.token : cred.key;

    if (token.empty())
    {
        throw std::runtime_error("[ChannelManager] Telegram credential has no key/token");
    }

    std::string proxy;
    bool replyToMessage = false;

    if (ch.raw.contains("proxy") && ch.raw["proxy"].is_string())
    {
        proxy = ch.raw["proxy"].get<std::string>();
    }

    if (ch.raw.contains("reply_to_message") && ch.raw["reply_to_message"].is_boolean())
    {
        replyToMessage = ch.raw["reply_to_message"].get<bool>();
    }

    std::string pubDir = config->publicDir;
    std::string pubUrl = config->server.publicUrl;
    telegramRunner = std::make_unique<TelegramRunner>(bus, sessionManager, taskManager, dispatcher, std::move(token), ch.allowedUsers, std::move(proxy), replyToMessage, std::move(pubDir), std::move(pubUrl));
    telegramRunner->start();
}

void ChannelManager::stopTelegram()
{
    if (telegramRunner)
    {
        telegramRunner->stop();
        telegramRunner.reset();
    }
}

void ChannelManager::startMcp()
{
    if (!mcpDispatcher)
    {
        throw std::runtime_error("[ChannelManager] MCP dispatcher not available");
    }
    mcpDispatcher->enable();
}

void ChannelManager::stopMcp()
{
    if (mcpDispatcher)
    {
        mcpDispatcher->disable();
    }
}

} // namespace channel
} // namespace ionclaw
