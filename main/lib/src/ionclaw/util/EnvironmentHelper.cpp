#include "ionclaw/util/EnvironmentHelper.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>

#include "spdlog/spdlog.h"

#include "ionclaw/util/FileHelper.hpp"
#include "ionclaw/util/StringHelper.hpp"

namespace ionclaw
{
namespace util
{

std::string EnvironmentHelper::expandEnvVars(const std::string &value)
{
    // pattern for ${VAR_NAME} environment variable references
    static thread_local const std::regex envPattern(R"(\$\{([^}]+)\})");

    // a single left-to-right pass over the original expands every reference and never re-scans a substituted value
    std::string result;
    result.reserve(value.size());

    size_t lastPos = 0;

    for (auto it = std::sregex_iterator(value.begin(), value.end(), envPattern); it != std::sregex_iterator(); ++it)
    {
        const auto &match = *it;
        auto position = static_cast<size_t>(match.position());

        result.append(value, lastPos, position - lastPos);

        const char *envValue = std::getenv(match[1].str().c_str());
        result += envValue ? envValue : "";

        lastPos = position + static_cast<size_t>(match.length());
    }

    result.append(value, lastPos, value.size() - lastPos);
    return result;
}

bool EnvironmentHelper::isSet(const std::string &name)
{
    return std::getenv(name.c_str()) != nullptr;
}

void EnvironmentHelper::set(const std::string &name, const std::string &value)
{
#if defined(_WIN32)
    _putenv_s(name.c_str(), value.c_str());
#else
    setenv(name.c_str(), value.c_str(), 1);
#endif
}

void EnvironmentHelper::unset(const std::string &name)
{
    // an empty value removes the variable on windows, matching unsetenv elsewhere
#if defined(_WIN32)
    _putenv_s(name.c_str(), "");
#else
    unsetenv(name.c_str());
#endif
}

void EnvironmentHelper::loadDotEnv(const std::string &projectPath)
{
    // the project .env is the source of truth, so it overrides any inherited environment
    for (const auto &[key, value] : readDotEnv(projectPath))
    {
        set(key, value);
    }
}

std::map<std::string, std::string> EnvironmentHelper::readDotEnv(const std::string &projectPath)
{
    std::map<std::string, std::string> values;
    std::ifstream file(std::filesystem::path(projectPath) / ".env");

    if (!file.is_open())
    {
        return values;
    }

    std::string line;

    while (std::getline(file, line))
    {
        auto entry = StringHelper::trim(line);

        if (entry.empty() || entry[0] == '#')
        {
            continue;
        }

        if (entry.starts_with("export "))
        {
            entry = StringHelper::trim(entry.substr(7));
        }

        auto separator = entry.find('=');

        if (separator == std::string::npos)
        {
            continue;
        }

        auto key = StringHelper::trim(entry.substr(0, separator));

        if (!key.empty())
        {
            values[key] = StringHelper::unquote(StringHelper::trim(entry.substr(separator + 1)));
        }
    }

    return values;
}

void EnvironmentHelper::writeDotEnv(const std::string &projectPath, const std::map<std::string, std::string> &values)
{
    auto path = std::filesystem::path(projectPath) / ".env";
    std::string content;

    for (const auto &[key, value] : values)
    {
        // quote values containing whitespace or comment markers so they round-trip cleanly
        bool quoted = value.find_first_of(" \t#") != std::string::npos;
        content += key + "=" + (quoted ? "\"" + value + "\"" : value) + "\n";
    }

    auto error = ionclaw::util::FileHelper::atomicWrite(path.string(), content);

    if (!error.empty())
    {
        spdlog::error("[EnvironmentHelper] {}", error);
    }
}

} // namespace util
} // namespace ionclaw
