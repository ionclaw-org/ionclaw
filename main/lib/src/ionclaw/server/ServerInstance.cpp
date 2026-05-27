#include "ionclaw/server/ServerInstance.hpp"

#include <filesystem>
#include <random>
#include <sstream>

#include "ionclaw/config/ConfigLoader.hpp"
#include "ionclaw/util/EmbeddedResources.hpp"
#include "ionclaw/util/EnvironmentHelper.hpp"

#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace server
{

std::shared_ptr<ionclaw::config::Config> ServerInstance::config;
std::shared_ptr<ionclaw::bus::EventDispatcher> ServerInstance::dispatcher;
std::shared_ptr<ionclaw::bus::MessageBus> ServerInstance::bus;
std::shared_ptr<ionclaw::session::SessionManager> ServerInstance::sessionManager;
std::shared_ptr<ionclaw::task::TaskManager> ServerInstance::taskManager;
std::shared_ptr<ionclaw::tool::ToolRegistry> ServerInstance::toolRegistry;
std::shared_ptr<WebSocketManager> ServerInstance::wsManager;
std::shared_ptr<Auth> ServerInstance::auth;
std::shared_ptr<ionclaw::agent::Orchestrator> ServerInstance::orchestrator;
std::shared_ptr<ionclaw::mcp::McpDispatcher> ServerInstance::mcpDispatcher;
std::shared_ptr<ionclaw::channel::ChannelManager> ServerInstance::channelManager;
std::shared_ptr<ionclaw::heartbeat::HeartbeatService> ServerInstance::heartbeatService;
std::shared_ptr<ionclaw::cron::CronService> ServerInstance::cronService;
std::shared_ptr<Routes> ServerInstance::routes;
std::shared_ptr<HttpServer> ServerInstance::httpServer;
std::mutex ServerInstance::mutex;

ServerResult ServerInstance::start(const std::string &projectPath, const std::string &host, int port, const std::string &rootPath, const std::string &webPath)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (httpServer)
    {
        return {"", 0, false, "server already running"};
    }

    try
    {
        // resolve project path
        auto resolvedPath = (!projectPath.empty()) ? projectPath : std::filesystem::current_path().string();

        resolvedPath = std::filesystem::absolute(resolvedPath).string();

        if (!std::filesystem::exists(resolvedPath) || !std::filesystem::is_directory(resolvedPath))
        {
            return {"", 0, false, "project path does not exist: " + resolvedPath};
        }

        spdlog::info("[ServerInstance] Project path: {}", resolvedPath);

        auto configPath = resolvedPath + "/config.yml";

        if (!std::filesystem::exists(configPath))
        {
            return {"", 0, false, "project not initialized at " + resolvedPath + " (config.yml not found)"};
        }

        // load the project .env into the environment before the config expands its ${VAR} references
        ionclaw::util::EnvironmentHelper::loadDotEnv(resolvedPath);

        // load configuration
        spdlog::info("[ServerInstance] Loading config from: {}", configPath);
        auto cfg = ionclaw::config::ConfigLoader::load(configPath);
        cfg.projectPath = resolvedPath;

        // ensure default credentials
        bool defaultsCreated = false;

        auto serverCredName = cfg.server.credential.empty() ? std::string("server") : cfg.server.credential;

        if (cfg.credentials.find(serverCredName) == cfg.credentials.end())
        {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, 15);
            std::ostringstream oss;
            const char hex[] = "0123456789abcdef";

            for (int i = 0; i < 64; ++i)
            {
                oss << hex[dis(gen)];
            }

            ionclaw::config::CredentialConfig serverCred;
            serverCred.type = "simple";
            serverCred.key = oss.str();
            cfg.credentials[serverCredName] = serverCred;
            cfg.server.credential = serverCredName;
            defaultsCreated = true;
            spdlog::info("[ServerInstance] Generated default server credential (JWT secret)");
        }

        auto webCredName = cfg.webClient.credential.empty() ? std::string("web_client") : cfg.webClient.credential;

        if (cfg.credentials.find(webCredName) == cfg.credentials.end())
        {
            ionclaw::config::CredentialConfig webCred;
            webCred.type = "login";
            webCred.username = "admin";
            webCred.password = "admin";
            cfg.credentials[webCredName] = webCred;
            cfg.webClient.credential = webCredName;
            defaultsCreated = true;
            spdlog::info("[ServerInstance] Generated default web client credential (admin/admin)");
        }

        if (defaultsCreated)
        {
            ionclaw::config::ConfigLoader::save(cfg, configPath);
            spdlog::info("[ServerInstance] Default credentials saved to config.yml");
        }

        // apply host and port overrides
        if (!host.empty())
        {
            cfg.server.host = host;
        }

        if (port > 0)
        {
            cfg.server.port = port;
        }

        // resolve agent workspaces
        ionclaw::config::ConfigLoader::resolveWorkspaces(cfg, resolvedPath);
        auto defaultWorkspace = resolvedPath + "/workspace";

        // set public directory path on config
        cfg.publicDir = resolvedPath + "/public";

        config = std::make_shared<ionclaw::config::Config>(cfg);

        // create core components
        dispatcher = std::make_shared<ionclaw::bus::EventDispatcher>();
        bus = std::make_shared<ionclaw::bus::MessageBus>();
        sessionManager = std::make_shared<ionclaw::session::SessionManager>(defaultWorkspace + "/sessions", cfg.sessionBudget.maxDiskBytes, cfg.sessionBudget.highWaterRatio);
        taskManager = std::make_shared<ionclaw::task::TaskManager>(defaultWorkspace + "/tasks.jsonl", dispatcher);
        toolRegistry = std::make_shared<ionclaw::tool::ToolRegistry>();
        wsManager = std::make_shared<WebSocketManager>();
        auth = std::make_shared<Auth>(cfg);

        // load persisted tasks and recover interrupted ones
        taskManager->load();
        taskManager->recoverStaleTasks();

        // connect event dispatcher to websocket
        auto wsRef = wsManager;
        // clang-format off
        dispatcher->addHandler([wsRef](const std::string &eventType, const nlohmann::json &data) { wsRef->broadcast(eventType, data); });
        // clang-format on

        // create orchestrator
        orchestrator = std::make_shared<ionclaw::agent::Orchestrator>(bus, dispatcher, sessionManager, taskManager, toolRegistry, cfg);

        // resolve web directory
        std::string webDir;

        // 1. explicit web path (e.g. from Flutter plugin)
        if (!webPath.empty())
        {
            auto candidate = std::filesystem::absolute(webPath).string();

            if (std::filesystem::exists(candidate + "/index.html"))
            {
                webDir = candidate;
            }
        }

        // 2. embedded resources (production: web client compiled into binary)
        if (webDir.empty() && ionclaw::util::EmbeddedResources::hasWebResources())
        {
            ionclaw::util::EmbeddedResources::loadWebResources();
            spdlog::info("[ServerInstance] Serving web client from embedded resources");
            // webDir stays empty → signals embedded mode to WebAppHandler
        }

        // 3. filesystem fallback (development / shared library)
        if (webDir.empty() && !ionclaw::util::EmbeddedResources::hasWebResources())
        {
            if (!rootPath.empty())
            {
                auto candidate = std::filesystem::absolute(rootPath).string() + "/web";

                if (std::filesystem::exists(candidate + "/index.html"))
                {
                    webDir = candidate;
                }
            }

            if (webDir.empty())
            {
                spdlog::warn("[ServerInstance] Web client not found, static file serving disabled");
            }
            else
            {
                spdlog::info("[ServerInstance] Serving web client from: {}", webDir);
            }
        }

        // public directory for static file serving
        auto publicDir = resolvedPath + "/public";
        spdlog::info("[ServerInstance] Serving public files from: {}", publicDir);

        // create MCP dispatcher
        mcpDispatcher = std::make_shared<ionclaw::mcp::McpDispatcher>(orchestrator, sessionManager, taskManager, bus, dispatcher, config);

        // create channel manager and start channels
        channelManager = std::make_shared<ionclaw::channel::ChannelManager>(config, bus, sessionManager, taskManager, dispatcher, mcpDispatcher);

        for (auto &[name, ch] : config->channels)
        {
            if (ch.enabled)
            {
                try
                {
                    channelManager->startChannel(name);
                    ch.running = true;
                }
                catch (const std::exception &e)
                {
                    spdlog::warn("[ServerInstance] Failed to start channel '{}': {}", name, e.what());
                }
            }
        }

        // create and start heartbeat service
        heartbeatService = std::make_shared<ionclaw::heartbeat::HeartbeatService>(bus, sessionManager, defaultWorkspace, cfg.heartbeat.interval, cfg.heartbeat.enabled, cfg.heartbeat.agent);
        heartbeatService->start();

        // create and start cron service
        cronService = std::make_shared<ionclaw::cron::CronService>(bus, taskManager, defaultWorkspace);
        cronService->start();

        // wire cron service into orchestrator for agent tool access
        orchestrator->setCronService(cronService);

        // create routes and http server
        routes = std::make_shared<Routes>(config, auth, orchestrator, channelManager, heartbeatService, cronService, sessionManager, taskManager, bus, dispatcher, wsManager, webDir, resolvedPath, publicDir, defaultWorkspace);

        httpServer = std::make_shared<HttpServer>(routes, auth, wsManager, mcpDispatcher, cfg.server, webDir, publicDir);

        // start services
        orchestrator->start();
        spdlog::info("[ServerInstance] Orchestrator started");

        httpServer->start();

        auto actualPort = httpServer->port();

        spdlog::info("[ServerInstance] Server listening on {}:{}", cfg.server.host, actualPort);

        // use localhost for display when binding to all interfaces
        auto displayHost = (cfg.server.host == "0.0.0.0") ? "localhost" : cfg.server.host;

        return {displayHost, actualPort, true};
    }
    catch (const std::exception &e)
    {
        spdlog::error("[ServerInstance] Failed to start server: {}", e.what());

        // stop services that may have been started before the failure
        if (orchestrator)
        {
            try
            {
                orchestrator->stop();
            }
            catch (const std::exception &stopErr)
            {
                spdlog::warn("[ServerInstance] Error stopping orchestrator: {}", stopErr.what());
            }
        }

        if (cronService)
        {
            try
            {
                cronService->stop();
            }
            catch (const std::exception &stopErr)
            {
                spdlog::warn("[ServerInstance] Error stopping cron service: {}", stopErr.what());
            }
        }

        if (heartbeatService)
        {
            try
            {
                heartbeatService->stop();
            }
            catch (const std::exception &stopErr)
            {
                spdlog::warn("[ServerInstance] Error stopping heartbeat service: {}", stopErr.what());
            }
        }

        if (channelManager)
        {
            try
            {
                channelManager->stopAll();
            }
            catch (const std::exception &stopErr)
            {
                spdlog::warn("[ServerInstance] Error stopping channels: {}", stopErr.what());
            }
        }

        resetComponents();

        return {"", 0, false, e.what()};
    }
}

ServerResult ServerInstance::stop()
{
    std::lock_guard<std::mutex> lock(mutex);

    if (!httpServer)
    {
        return {"", 0, false, "server not running"};
    }

    try
    {
        // set abort cutoff on all active sessions before shutdown
        sessionManager->setAbortCutoffAll();

        // shutdown services
        httpServer->stop();

        channelManager->stopAll();
        heartbeatService->stop();
        cronService->stop();
        orchestrator->stop();
        spdlog::info("[ServerInstance] Orchestrator stopped");

        taskManager->save();
        spdlog::info("[ServerInstance] Tasks saved");

        resetComponents();

        return {"", 0, true};
    }
    catch (const std::exception &e)
    {
        spdlog::error("[ServerInstance] Failed to stop server: {}", e.what());

        // force release all components to avoid stuck state
        resetComponents();

        return {"", 0, false, e.what()};
    }
}

void ServerInstance::resetComponents()
{
    httpServer.reset();
    routes.reset();
    cronService.reset();
    heartbeatService.reset();
    channelManager.reset();
    mcpDispatcher.reset();
    orchestrator.reset();
    auth.reset();
    wsManager.reset();
    toolRegistry.reset();
    taskManager.reset();
    sessionManager.reset();
    bus.reset();
    dispatcher.reset();
    config.reset();
}

} // namespace server
} // namespace ionclaw
