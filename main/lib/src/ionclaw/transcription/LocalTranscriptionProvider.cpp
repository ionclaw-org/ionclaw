#include "ionclaw/transcription/LocalTranscriptionProvider.hpp"

#include <array>
#include <filesystem>
#include <fstream>

#include "ionclaw/tool/builtin/ToolHelper.hpp"
#include "ionclaw/util/PipeGuard.hpp"
#include "ionclaw/util/StringHelper.hpp"
#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace transcription
{

namespace fs = std::filesystem;

LocalTranscriptionProvider::LocalTranscriptionProvider(const std::string &providerName)
    : providerName_(providerName)
{
}

std::string LocalTranscriptionProvider::providerName() const
{
    return providerName_;
}

TranscriptionResult LocalTranscriptionProvider::transcribe(const std::string &audioData,
                                                           const std::string &format,
                                                           const TranscriptionContext &context) const
{
    // write audio to temporary file
    auto tmpDir = fs::temp_directory_path();
    auto tmpFile = tmpDir / ("ionclaw_audio_" + std::to_string(std::hash<std::string>{}(audioData.substr(0, 64))) + "." + format);

    {
        std::ofstream ofs(tmpFile, std::ios::binary);

        if (!ofs.is_open())
        {
            spdlog::error("[local-transcription] failed to write temp file: {}", tmpFile.string());
            return {};
        }

        ofs.write(audioData.data(), static_cast<std::streamsize>(audioData.size()));
    }

    // strip provider prefix from model to get binary name or model size
    // e.g. "local/base" → "base", "local/whisper" → "whisper"
    auto model = context.model;
    auto slashPos = model.find('/');

    if (slashPos != std::string::npos)
    {
        model = model.substr(slashPos + 1);
    }

    // build command: try whisper CLI (openai-whisper pip package)
    // output format json gives structured output with text, language, etc.
    std::string command = "whisper \"" + tmpFile.string() + "\""
                                                            " --output_format json"
                                                            " --output_dir \"" +
                          tmpDir.string() + "\"";

    if (!model.empty() && model != "whisper")
    {
        command += " --model " + ionclaw::tool::builtin::ToolHelper::shellEscape(model);
    }

    if (!context.language.empty())
    {
        command += " --language " + ionclaw::tool::builtin::ToolHelper::shellEscape(context.language);
    }

    // redirect stderr to /dev/null to only capture clean output
#ifdef _WIN32
    command += " 2>NUL";
#else
    command += " 2>/dev/null";
#endif

    spdlog::info("[local-transcription] running: {}", command);

    // execute via popen (RAII guard ensures pclose on all exit paths)
    ionclaw::util::PipeGuard pipe(command.c_str());

    if (!pipe)
    {
        spdlog::error("[local-transcription] failed to execute whisper command");
        fs::remove(tmpFile);
        return {};
    }

    std::string output;
    std::array<char, 4096> buffer{};

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr)
    {
        output += buffer.data();

        if (output.size() > 1024 * 1024)
        {
            break; // safety limit: 1MB
        }
    }

    auto exitCode = pipe.close();

    // whisper outputs to a .json file next to the audio, named <stem>.json
    auto jsonFile = tmpDir / (tmpFile.stem().string() + ".json");

    TranscriptionResult result;

    if (fs::exists(jsonFile))
    {
        try
        {
            std::ifstream jf(jsonFile);
            auto json = nlohmann::json::parse(jf);

            result.text = json.value("text", "");

            if (json.contains("language"))
            {
                result.language = json["language"].get<std::string>();
            }

            // sum segment durations if available
            if (json.contains("segments") && json["segments"].is_array())
            {
                for (const auto &seg : json["segments"])
                {
                    if (seg.contains("end"))
                    {
                        double end = seg["end"].get<double>();

                        if (end > result.durationSeconds)
                        {
                            result.durationSeconds = end;
                        }
                    }
                }
            }
        }
        catch (const std::exception &e)
        {
            spdlog::error("[local-transcription] failed to parse JSON output: {}", e.what());
        }

        fs::remove(jsonFile);
    }
    else if (!output.empty())
    {
        // fallback: whisper might print text to stdout
        result.text = output;
    }

    // cleanup
    fs::remove(tmpFile);

    // also remove .txt/.srt/.vtt files that whisper may have generated
    for (const auto &ext : {".txt", ".srt", ".vtt", ".tsv"})
    {
        auto extra = tmpDir / (tmpFile.stem().string() + ext);

        if (fs::exists(extra))
        {
            fs::remove(extra);
        }
    }

    if (exitCode != 0 && result.text.empty())
    {
        spdlog::error("[local-transcription] whisper exited with code {}", exitCode);
        return {};
    }

    // trim whitespace
    auto start = result.text.find_first_not_of(" \t\n\r");
    auto end = result.text.find_last_not_of(" \t\n\r");

    if (start != std::string::npos && end != std::string::npos)
    {
        result.text = result.text.substr(start, end - start + 1);
    }

    spdlog::info("[local-transcription] transcribed {:.1f}s (lang={}): {}",
                 result.durationSeconds, result.language,
                 ionclaw::util::StringHelper::utf8SafeTruncate(result.text, 100));

    return result;
}

} // namespace transcription
} // namespace ionclaw
