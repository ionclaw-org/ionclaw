#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ionclaw/config/Config.hpp"
#include "ionclaw/provider/LlmProvider.hpp"
#include "nlohmann/json.hpp"

namespace ionclaw
{
namespace provider
{

class ProviderFactory
{
public:
    static std::shared_ptr<LlmProvider> create(const std::string &providerName, const std::string &apiKey, const std::string &baseUrl = "", int timeout = 60, const std::map<std::string, std::string> &extraHeaders = {});
    static std::shared_ptr<LlmProvider> createFromModel(const std::string &model, const ionclaw::config::Config &config);
    static std::shared_ptr<LlmProvider> createFailoverFromProfiles(const std::vector<ionclaw::config::ProfileConfig> &profiles, const std::string &defaultModel, const ionclaw::config::Config &config);

private:
    static std::string defaultBaseUrl(const std::string &providerName);
    static std::shared_ptr<LlmProvider> makeLlamaProvider(const std::string &modelPath, const nlohmann::json &params);
    static std::string fileNameOf(const std::string &path);
    static bool startsWithCaseInsensitive(const std::string &text, const std::string &prefix);
    static bool isModelFile(const std::string &fileName);
    static const ionclaw::config::ProviderConfig &resolveLlamaProvider(const ionclaw::config::Config &config, const std::string &requested);
};

} // namespace provider
} // namespace ionclaw
