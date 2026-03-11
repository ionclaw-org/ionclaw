#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ionclaw/config/Config.hpp"
#include "ionclaw/provider/LlmProvider.hpp"
#include "ionclaw/session/SessionManager.hpp"

namespace ionclaw
{
namespace agent
{

class Classifier
{
public:
    Classifier(std::shared_ptr<ionclaw::provider::LlmProvider> provider, const ionclaw::config::Config &config);

    // when set, only these agents are considered for routing
    void setActiveAgents(const std::vector<std::string> &agents);

    std::string classify(
        const std::string &message,
        const std::string &sessionKey,
        const std::vector<ionclaw::session::SessionMessage> &history = {}) const;

private:
    std::shared_ptr<ionclaw::provider::LlmProvider> provider;
    const ionclaw::config::Config &config;
    std::vector<std::string> activeAgents;
};

} // namespace agent
} // namespace ionclaw
