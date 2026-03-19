#include "ionclaw/Version.hpp"
#include "ionclaw/server/ServerInstance.hpp"
#include "ionclaw/util/EmbeddedResources.hpp"

#include "spdlog/spdlog.h"

#ifdef IONCLAW_HAS_SSL
#include "Poco/Net/SSLManager.h"
#endif

#include <csignal>
#include <filesystem>
#include <iostream>
#include <thread>

namespace
{

class Application
{
public:
    static int run(int argc, char *argv[]);

private:
    static std::atomic<bool> shutdownRequested;

    static void signalHandler(int signal);
    static void printUsage();
    static int cmdStart(int argc, char *argv[]);
    static int cmdInit(int argc, char *argv[]);
};

std::atomic<bool> Application::shutdownRequested{false};

void Application::signalHandler(int /* signal */)
{
    // only async-signal-safe operations here (no spdlog, no malloc)
    shutdownRequested.store(true);
}

void Application::printUsage()
{
    std::cout << "IonClaw v" << ionclaw::VERSION << " - AI Agent Orchestrator\n"
              << "\n"
              << "Usage: ionclaw <command> [options]\n"
              << "\n"
              << "Commands:\n"
              << "  start              Start the server\n"
              << "  init [path]        Initialize a new project from template\n"
              << "  version            Show version\n"
              << "\n"
              << "Options (start):\n"
              << "  --project <path>   Project path (default: current directory)\n"
              << "  --host <host>      Server host (default: from config)\n"
              << "  --port <port>      Server port (default: from config)\n"
              << "  --debug            Enable debug logging\n"
              << "  --help             Show this help\n";
}

int Application::cmdStart(int argc, char *argv[])
{
    std::string projectPath;
    std::string host;
    int port = 0;
    bool debug = false;

    // parse command-line arguments
    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];

        if (arg == "start")
        {
            continue;
        }
        else if (arg == "--project" && i + 1 < argc)
        {
            projectPath = argv[++i];
        }
        else if (arg == "--host" && i + 1 < argc)
        {
            host = argv[++i];
        }
        else if (arg == "--port" && i + 1 < argc)
        {
            try
            {
                port = std::stoi(argv[++i]);
            }
            catch (const std::exception &e)
            {
                spdlog::error("Invalid port number: {}", e.what());
                return EXIT_FAILURE;
            }
        }
        else if (arg == "--debug")
        {
            debug = true;
        }
    }

    // logging
    spdlog::set_level(debug ? spdlog::level::debug : spdlog::level::info);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");

    spdlog::info("IonClaw v{} - AI Agent Orchestrator", ionclaw::VERSION);

    // start server
    auto rootPath = std::filesystem::weakly_canonical(std::filesystem::path(argv[0])).parent_path().string();
    auto result = ionclaw::server::ServerInstance::start(projectPath, host, port, rootPath);

    if (!result.success)
    {
        spdlog::error("Failed to start server: {}", result.error);
        return EXIT_FAILURE;
    }

    spdlog::info("Open http://{}:{} in your browser", result.host, result.port);

    // signal handling
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);
#endif

    // wait for shutdown
    while (!shutdownRequested.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // stop server
    spdlog::info("Shutting down...");
    ionclaw::server::ServerInstance::stop();
    spdlog::info("IonClaw stopped");

    return EXIT_SUCCESS;
}

int Application::cmdInit(int argc, char *argv[])
{
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");

    // parse target directory
    std::string targetDir;

    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];

        if (arg == "init")
        {
            continue;
        }
        else if (!arg.empty() && arg[0] != '-')
        {
            targetDir = arg;
        }
    }

    if (targetDir.empty())
    {
        targetDir = std::filesystem::current_path().string();
    }

    targetDir = std::filesystem::absolute(targetDir).string();

    // check if already initialized
    auto configPath = targetDir + "/config.yml";

    if (std::filesystem::exists(configPath))
    {
        std::cout << "Project already initialized at: " << targetDir << "\n";
        return EXIT_SUCCESS;
    }

    // initialize project
    std::cout << "Initializing project at: " << targetDir << "\n";

    bool success = ionclaw::util::EmbeddedResources::extractTemplate(targetDir);

    if (success)
    {
        // create workspace directories
        std::error_code ec;
        std::filesystem::create_directories(targetDir + "/workspace/sessions", ec);
        std::filesystem::create_directories(targetDir + "/workspace/skills", ec);
        std::filesystem::create_directories(targetDir + "/workspace/memory", ec);
        std::filesystem::create_directories(targetDir + "/public", ec);
        std::filesystem::create_directories(targetDir + "/skills", ec);

        std::cout << "Project initialized successfully.\n";
        std::cout << "Run 'ionclaw start --project " << targetDir << "' to start.\n";
        return EXIT_SUCCESS;
    }

    std::cerr << "Failed to initialize project.\n";
    return EXIT_FAILURE;
}

int Application::run(int argc, char *argv[])
{
    // dispatch command
    std::string command;

    if (argc > 1)
    {
        std::string arg = argv[1];

        if (arg == "version" || arg == "--version" || arg == "-v")
        {
            std::cout << "ionclaw " << ionclaw::VERSION << "\n";
            return EXIT_SUCCESS;
        }

        if (arg == "--help" || arg == "-h")
        {
            printUsage();
            return EXIT_SUCCESS;
        }

        if (arg == "init")
        {
            command = "init";
        }
        else if (arg == "start")
        {
            command = "start";
        }
    }

    if (command.empty())
    {
        printUsage();
        return EXIT_SUCCESS;
    }

    if (command == "init")
    {
        return cmdInit(argc, argv);
    }

    return cmdStart(argc, argv);
}

} // namespace

int main(int argc, char *argv[])
{
#ifdef IONCLAW_HAS_SSL
    Poco::Net::initializeSSL();
#endif

    int result = Application::run(argc, argv);

#ifdef IONCLAW_HAS_SSL
    Poco::Net::uninitializeSSL();
#endif

    return result;
}
