#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ionclaw/transcription/TranscriptionProvider.hpp"

namespace ionclaw
{
namespace transcription
{

class TranscriptionProviderRegistry
{
public:
    static TranscriptionProviderRegistry &instance();

    void registerProvider(const std::string &name, std::unique_ptr<TranscriptionProvider> provider);
    TranscriptionProvider *get(const std::string &name) const;
    std::vector<std::string> providerNames() const;

private:
    TranscriptionProviderRegistry();
    std::unordered_map<std::string, std::unique_ptr<TranscriptionProvider>> providers_;
};

} // namespace transcription
} // namespace ionclaw
