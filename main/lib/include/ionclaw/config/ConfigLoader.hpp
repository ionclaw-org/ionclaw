#pragma once

#include <map>
#include <string>
#include <vector>

#include "ionclaw/config/Config.hpp"
#include "nlohmann/json.hpp"
#include "yaml-cpp/yaml.h"

namespace ionclaw
{
namespace config
{

class ConfigLoader
{
public:
    static Config load(const std::string &path);
    static Config loadFromString(const std::string &yaml);
    static void save(const Config &config, const std::string &path);
    static std::string toYaml(const Config &config);
    static std::string expandEnvVars(const std::string &value);

    // resolves agent workspaces: ensures at least one agent exists,
    // sets empty workspaces to the default, resolves relative to absolute
    static void resolveWorkspaces(Config &config, const std::string &projectPath);

private:
    static Config loadFromNode(const YAML::Node &root);
    static std::string expandStr(const YAML::Node &node, const std::string &defaultValue = "");
    static int expandInt(const YAML::Node &node, int defaultValue = 0);
    static bool expandBool(const YAML::Node &node, bool defaultValue = false);
    static std::vector<std::string> expandStringList(const YAML::Node &node);
    static std::map<std::string, std::string> expandStringMap(const YAML::Node &node);
    static nlohmann::json yamlToJson(const YAML::Node &node);
    static void emitJson(YAML::Emitter &out, const nlohmann::json &j);
};

} // namespace config
} // namespace ionclaw
