#pragma once

#include <string>

#include "ionclaw/transcription/TranscriptionProvider.hpp"

namespace ionclaw
{
namespace transcription
{

class LocalTranscriptionProvider : public TranscriptionProvider
{
public:
    explicit LocalTranscriptionProvider(const std::string &providerName);

    std::string providerName() const override;
    TranscriptionResult transcribe(const std::string &audioData,
                                   const std::string &format,
                                   const TranscriptionContext &context) const override;

private:
    std::string providerName_;
};

} // namespace transcription
} // namespace ionclaw
