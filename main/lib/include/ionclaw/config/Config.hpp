#pragma once

#include <map>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace ionclaw
{
namespace config
{

struct BotConfig
{
    std::string name = "IonClaw";
    std::string description;
};

struct ServerConfig
{
    std::string host = "0.0.0.0";
    int port = 8080;
    std::string publicUrl;
    std::string credential = "server";
};

struct AgentParams
{
    int maxIterations = 40;
    int maxConcurrent = 1;
    int maxHistory = 500;
    int contextTokens = 0;
    std::map<std::string, int> channelHistoryLimits;
};

struct ProfileConfig
{
    std::string model;
    std::string credential;
    std::string baseUrl;
    int priority = 0;
    nlohmann::json modelParams;
};

struct SubagentLimits
{
    int maxDepth = 5;
    int maxChildren = 5;
    int defaultTimeoutSeconds = 0;
    std::vector<std::string> allowAgents;
};

struct ToolPolicy
{
    std::vector<std::string> allow;
    std::vector<std::string> deny;
};

struct AgentConfig
{
    std::string model;
    std::string description;
    std::string instructions;
    std::string workspace;
    std::vector<std::string> tools;
    ToolPolicy toolPolicy;
    AgentParams agentParams;
    nlohmann::json modelParams;
    std::vector<ProfileConfig> profiles;
    SubagentLimits subagentLimits;
};

struct CredentialConfig
{
    std::string type = "simple";
    std::string key;
    std::string username;
    std::string password;
    std::string token;
    nlohmann::json raw;
};

struct ProviderConfig
{
    std::string name;
    std::string credential;
    std::string baseUrl;
    int timeout = 60;
    std::map<std::string, std::string> requestHeaders;
    nlohmann::json modelParams;
};

struct WebClientConfig
{
    std::string credential = "web_client";
};

struct ImageConfig
{
    std::string model;
    std::string aspectRatio;
    std::string size;
};

struct TranscriptionConfig
{
    std::string model;
};

struct ToolsConfig
{
    bool restrictToWorkspace = true;
    int execTimeout = 60;
    std::string webSearchProvider;
    std::string webSearchCredential;
    int webSearchMaxResults = 5;
};

struct StorageConfig
{
    std::string type = "local";
};

struct ChannelConfig
{
    bool enabled = false;
    bool running = false;
    std::string credential;
    std::vector<std::string> allowedUsers;
    nlohmann::json raw;
};

struct ClassifierConfig
{
    std::string model;
};

struct SessionBudgetConfig
{
    int64_t maxDiskBytes = 0;
    double highWaterRatio = 0.8;
};

struct HeartbeatConfig
{
    bool enabled = false;
    int interval = 1800;
    std::string agent;
};

struct MessageQueueConfig
{
    std::string mode = "collect";
    std::map<std::string, std::string> byChannel;
    int debounceMs = 1000;
    int cap = 20;
    std::string dropPolicy = "summarize";
};

struct MessagesConfig
{
    MessageQueueConfig queue;
};

struct Config
{
    std::string projectPath;
    std::string publicDir;

    BotConfig bot;
    ServerConfig server;
    WebClientConfig webClient;
    ClassifierConfig classifier;
    ImageConfig image;
    TranscriptionConfig transcription;
    ToolsConfig tools;
    StorageConfig storage;
    std::map<std::string, AgentConfig> agents;
    std::map<std::string, CredentialConfig> credentials;
    std::map<std::string, ProviderConfig> providers;
    std::map<std::string, ChannelConfig> channels;
    HeartbeatConfig heartbeat;
    SessionBudgetConfig sessionBudget;
    MessagesConfig messages;
    nlohmann::json forms;

    std::string resolveApiKey(const std::string &providerName) const;
    std::string resolveBaseUrl(const std::string &providerName) const;
    const ProviderConfig *findProvider(const std::string &model) const;
    ProviderConfig resolveProvider(const std::string &model) const;
};

} // namespace config
} // namespace ionclaw
