#pragma once

#include <string>

namespace ionclaw
{
namespace config
{
struct Config;
}

namespace transcription
{

struct TranscriptionResult
{
    std::string text;
    std::string language;
    double durationSeconds = 0.0;
};

struct TranscriptionContext
{
    std::string model;
    std::string providerName;
    std::string language;
    const ionclaw::config::Config *config = nullptr;
};

class TranscriptionProvider
{
public:
    virtual ~TranscriptionProvider() = default;

    virtual std::string providerName() const = 0;
    virtual TranscriptionResult transcribe(const std::string &audioData,
                                           const std::string &format,
                                           const TranscriptionContext &context) const = 0;
};

} // namespace transcription
} // namespace ionclaw
