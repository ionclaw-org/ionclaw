#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace ionclaw
{
namespace config
{
struct Config;
}
namespace bus
{
class MessageBus;
struct OutboundMessage;
}
} // namespace ionclaw

namespace ionclaw
{
namespace channel
{

struct OutboundRequest;

// delivers the agent's replies to whatsapp by consuming the "whatsapp" outbound queue and calling the provider api
class WhatsAppRunner
{
public:
    WhatsAppRunner(std::shared_ptr<ionclaw::config::Config> config, std::shared_ptr<ionclaw::bus::MessageBus> bus);
    ~WhatsAppRunner();

    void start();
    void stop();

private:
    void outboundLoop();
    void send(const ionclaw::bus::OutboundMessage &msg);

    // executes a built provider request, logging any non-2xx response against the chat id
    void execute(const OutboundRequest &req, const std::string &chatId);

    // splits a long reply into utf-8-safe chunks under the whatsapp message limit
    static std::vector<std::string> splitMessage(const std::string &text, size_t maxChars);

    // turns a workspace media path into its public url, passing absolute urls through unchanged
    std::string resolvePublicUrl(const std::string &path) const;

    // returns the last path segment, used as the outbound document file name
    static std::string baseName(const std::string &path);

    std::shared_ptr<ionclaw::config::Config> config;
    std::shared_ptr<ionclaw::bus::MessageBus> bus;
    std::atomic<bool> running{false};
    std::thread outboundThread;
};

} // namespace channel
} // namespace ionclaw
