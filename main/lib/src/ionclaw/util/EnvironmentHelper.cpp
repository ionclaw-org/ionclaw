#include "ionclaw/util/EnvironmentHelper.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>

#include "spdlog/spdlog.h"

#include "ionclaw/util/StringHelper.hpp"

namespace ionclaw
{
namespace util
{

std::string EnvironmentHelper::expandEnvVars(const std::string &value)
{
    // pattern for ${VAR_NAME} environment variable references
    static thread_local const std::regex envPattern(R"(\$\{([^}]+)\})");

    std::string result = value;
    std::smatch match;
    int iterations = 0;
    static constexpr int MAX_EXPANSION_ITERATIONS = 20;

    while (std::regex_search(result, match, envPattern) && iterations < MAX_EXPANSION_ITERATIONS)
    {
        const char *envValue = std::getenv(match[1].str().c_str());
        std::string replacement = envValue ? envValue : "";
        result = match.prefix().str() + replacement + match.suffix().str();
        ++iterations;
    }

    if (iterations >= MAX_EXPANSION_ITERATIONS)
    {
        spdlog::warn("[EnvironmentHelper] Environment variable expansion hit iteration limit for '{}'", value);
    }

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

        if (entry.rfind("export ", 0) == 0)
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
    std::ofstream file(path, std::ios::trunc);

    if (!file.is_open())
    {
        spdlog::error("[EnvironmentHelper] Failed to write .env at {}", path.string());
        return;
    }

    for (const auto &[key, value] : values)
    {
        // quote values containing whitespace or comment markers so they round-trip cleanly
        bool quoted = value.find_first_of(" \t#") != std::string::npos;
        file << key << "=" << (quoted ? "\"" + value + "\"" : value) << "\n";
    }
}

} // namespace util
} // namespace ionclaw
