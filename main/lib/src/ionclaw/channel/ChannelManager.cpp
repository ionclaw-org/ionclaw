#include "ionclaw/channel/ChannelManager.hpp"

#include "ionclaw/channel/TelegramRunner.hpp"
#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace channel
{

ChannelManager::ChannelManager(
    std::shared_ptr<ionclaw::config::Config> config,
    std::shared_ptr<ionclaw::bus::MessageBus> bus,
    std::shared_ptr<ionclaw::session::SessionManager> sessionManager,
    std::shared_ptr<ionclaw::task::TaskManager> taskManager,
    std::shared_ptr<ionclaw::bus::EventDispatcher> dispatcher)
    : config(std::move(config))
    , bus(std::move(bus))
    , sessionManager(std::move(sessionManager))
    , taskManager(std::move(taskManager))
    , dispatcher(std::move(dispatcher))
{
}

ChannelManager::~ChannelManager() = default;

void ChannelManager::startChannel(const std::string &name)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (name == "telegram")
    {
        startTelegram();
    }
}

void ChannelManager::stopChannel(const std::string &name)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (name == "telegram")
    {
        stopTelegram();
    }
}

void ChannelManager::stopAll()
{
    std::lock_guard<std::mutex> lock(mutex);
    stopTelegram();
}

void ChannelManager::startTelegram()
{
    if (telegramRunner)
    {
        return;
    }
    auto it = config->channels.find("telegram");
    if (it == config->channels.end())
    {
        throw std::runtime_error("Telegram channel not in config");
    }
    const auto &ch = it->second;
    if (ch.credential.empty())
    {
        throw std::runtime_error("Telegram credential not set");
    }
    auto credIt = config->credentials.find(ch.credential);
    if (credIt == config->credentials.end())
    {
        throw std::runtime_error("Telegram credential '" + ch.credential + "' not found");
    }
    const auto &cred = credIt->second;
    std::string token = cred.key.empty() ? cred.token : cred.key;
    if (token.empty())
    {
        throw std::runtime_error("Telegram credential has no key/token");
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
    telegramRunner = std::make_unique<TelegramRunner>(
        bus, sessionManager, taskManager, dispatcher,
        std::move(token), ch.allowedUsers, std::move(proxy), replyToMessage, std::move(pubDir));
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

} // namespace channel
} // namespace ionclaw
