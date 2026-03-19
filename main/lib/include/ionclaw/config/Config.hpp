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
    int contextTokens = 0;                           // global context window cap (0 = no cap, use model limit)
    std::map<std::string, int> channelHistoryLimits; // per-channel override, e.g. {"telegram": 50, "web": 200}
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
    int defaultTimeoutSeconds = 0;        // 0 = use registry default (300s)
    std::vector<std::string> allowAgents; // empty = same agent only, "*" = all
};

struct ToolPolicy
{
    std::vector<std::string> allow; // empty = all allowed
    std::vector<std::string> deny;  // deny takes precedence over allow
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
    bool restrictToWorkspace = true; // restrict file tools to workspace directory
    int execTimeout = 60;
    std::string webSearchProvider;   // provider name (e.g. brave, duckduckgo)
    std::string webSearchCredential; // credential reference (key in credentials.*)
    int webSearchMaxResults = 5;     // default max results for web search (1-10)
};

struct StorageConfig
{
    std::string type = "local";
};

struct ChannelConfig
{
    bool enabled = false; // persisted: auto-start on server boot
    bool running = false; // transient: actual runtime state
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
    int64_t maxDiskBytes = 0; // 0 = unlimited
    double highWaterRatio = 0.8;
};

struct HeartbeatConfig
{
    bool enabled = false;
    int interval = 1800;
};

struct MessageQueueConfig
{
    std::string mode = "collect";                 // default queue mode
    std::map<std::string, std::string> byChannel; // per-channel mode override
    int debounceMs = 1000;                        // collect debounce (ms)
    int cap = 20;                                 // max queued messages before drop
    std::string dropPolicy = "summarize";         // old | new | summarize
};

struct MessagesConfig
{
    MessageQueueConfig queue;
};

struct Config
{
    std::string projectPath;
    std::string publicDir; // resolved path to public/ directory

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
    ProviderConfig resolveProvider(const std::string &model) const;
};

} // namespace config
} // namespace ionclaw
