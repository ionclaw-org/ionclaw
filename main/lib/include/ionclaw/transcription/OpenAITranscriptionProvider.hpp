#pragma once

#include <sstream>
#include <string>

#include "Poco/Net/PartSource.h"

#include "ionclaw/transcription/TranscriptionProvider.hpp"

namespace ionclaw
{
namespace transcription
{

class OpenAITranscriptionProvider final : public TranscriptionProvider
{
public:
    explicit OpenAITranscriptionProvider(const std::string &providerName);

    std::string providerName() const override;
    TranscriptionResult transcribe(const std::string &audioData,
                                   const std::string &format,
                                   const TranscriptionContext &context) const override;

private:
    std::string providerName_;

    static std::string audioMimeType(const std::string &format);
    static std::string stripModelPrefix(const std::string &model);

    // in-memory part source for multipart form uploads
    class StringPartSource : public Poco::Net::PartSource
    {
    public:
        StringPartSource(const std::string &data, const std::string &mediaType, const std::string &filename);
        std::istream &stream() override;
        const std::string &filename() const override;

    private:
        std::string data_;
        std::istringstream stream_;
        std::string filename_;
    };
};

} // namespace transcription
} // namespace ionclaw
