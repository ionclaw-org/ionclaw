#include "ionclaw/provider/ModelCapabilities.hpp"

#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

#include "ionclaw/util/EmbeddedResources.hpp"

namespace ionclaw
{
namespace provider
{

std::once_flag ModelCapabilities::loadFlag;
std::unordered_map<std::string, ModelInfo> ModelCapabilities::table;

void ModelCapabilities::ensureLoaded()
{
    // clang-format off
    std::call_once(loadFlag, []() {
        auto raw = ionclaw::util::EmbeddedResources::getModelCapabilitiesJson();

        if (raw.empty())
        {
            spdlog::warn("[ModelCapabilities] Capability table is empty");
            return;
        }

        try
        {
            auto data = nlohmann::json::parse(raw);

            for (auto it = data.begin(); it != data.end(); ++it)
            {
                // the file carries a documentation-only sample entry that is not a real model
                if (it.key() == "sample_spec" || !it.value().is_object())
                {
                    continue;
                }

                const auto &v = it.value();
                ModelInfo info;
                info.provider = v.value("litellm_provider", "");
                info.maxInputTokens = v.value("max_input_tokens", int64_t{0});
                info.supportsVision = v.value("supports_vision", false);
                info.supportsFunctionCalling = v.value("supports_function_calling", false);
                info.supportsReasoning = v.value("supports_reasoning", false);
                info.supportsPromptCaching = v.value("supports_prompt_caching", false);

                table[it.key()] = std::move(info);
            }

            spdlog::info("[ModelCapabilities] Loaded {} models", table.size());
        }
        catch (const std::exception &e)
        {
            spdlog::error("[ModelCapabilities] Failed to parse capability table: {}", e.what());
        }
    });
    // clang-format on
}

std::optional<ModelInfo> ModelCapabilities::lookup(const std::string &model)
{
    ensureLoaded();

    auto it = table.find(model);

    if (it != table.end())
    {
        return it->second;
    }

    // the table keys most models by their bare name, so retry after dropping a provider/ prefix
    auto slash = model.find('/');

    if (slash != std::string::npos)
    {
        auto rest = model.substr(slash + 1);
        auto restIt = table.find(rest);

        if (restIt != table.end())
        {
            return restIt->second;
        }
    }

    return std::nullopt;
}

bool ModelCapabilities::supportsVision(const std::string &model, bool defaultIfUnknown)
{
    auto info = lookup(model);
    return info ? info->supportsVision : defaultIfUnknown;
}

bool ModelCapabilities::supportsReasoning(const std::string &model, bool defaultIfUnknown)
{
    auto info = lookup(model);
    return info ? info->supportsReasoning : defaultIfUnknown;
}

bool ModelCapabilities::supportsFunctionCalling(const std::string &model, bool defaultIfUnknown)
{
    auto info = lookup(model);
    return info ? info->supportsFunctionCalling : defaultIfUnknown;
}

bool ModelCapabilities::supportsPromptCaching(const std::string &model, bool defaultIfUnknown)
{
    auto info = lookup(model);
    return info ? info->supportsPromptCaching : defaultIfUnknown;
}

int64_t ModelCapabilities::contextWindow(const std::string &model)
{
    auto info = lookup(model);
    return info ? info->maxInputTokens : 0;
}

} // namespace provider
} // namespace ionclaw
