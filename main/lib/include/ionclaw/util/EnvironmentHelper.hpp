#pragma once

#include <map>
#include <string>

namespace ionclaw
{
namespace util
{

class EnvironmentHelper
{
public:
    static std::string expandEnvVars(const std::string &value);
    static bool isSet(const std::string &name);
    static void set(const std::string &name, const std::string &value);
    static void unset(const std::string &name);

    static void loadDotEnv(const std::string &projectPath);
    static std::map<std::string, std::string> readDotEnv(const std::string &projectPath);
    static void writeDotEnv(const std::string &projectPath, const std::map<std::string, std::string> &values);
};

} // namespace util
} // namespace ionclaw
