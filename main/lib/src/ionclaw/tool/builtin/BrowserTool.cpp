#include "ionclaw/tool/builtin/BrowserTool.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef IONCLAW_HAS_STB_IMAGE_WRITE
#include "stb_image.h"
#include "stb_image_resize2.h"
#include "stb_image_write.h"
#endif

#ifndef _WIN32
#include <signal.h>
#if !defined(__ANDROID__)
#include <spawn.h>
extern char **environ;
#endif
#endif

#include "Poco/Net/HTTPClientSession.h"
#include "Poco/Net/HTTPRequest.h"
#include "Poco/Net/HTTPResponse.h"
#include "Poco/Net/WebSocket.h"
#include "Poco/StreamCopier.h"
#include "Poco/URI.h"
#include "spdlog/spdlog.h"

#include "ionclaw/tool/builtin/ToolHelper.hpp"
#include "ionclaw/util/HttpClient.hpp"
#include "ionclaw/util/StringHelper.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

namespace
{

// ── CDP Protocol Registry ─────────────────────────────────────────────────
// Central registry of all Chrome DevTools Protocol methods and events.
// When CDP protocol changes, update the string values below.
// All usages reference these constants, so changes propagate automatically.
// Ref: https://chromedevtools.github.io/devtools-protocol/
//
// Note: Strings use adjacent literal concatenation ("Domain" ".method")
// to prevent automated refactoring from modifying these definitions.

namespace cdp
{

namespace page
{
constexpr const char *Enable = "Page"
                               ".enable";
constexpr const char *Navigate = "Page"
                                 ".navigate";
constexpr const char *Reload = "Page"
                               ".reload";
constexpr const char *GetNavigationHistory = "Page"
                                             ".getNavigationHistory";
constexpr const char *NavigateToHistoryEntry = "Page"
                                               ".navigateToHistoryEntry";
constexpr const char *GetLayoutMetrics = "Page"
                                         ".getLayoutMetrics";
constexpr const char *CaptureScreenshot = "Page"
                                          ".captureScreenshot";
constexpr const char *PrintToPDF = "Page"
                                   ".printToPDF";
constexpr const char *HandleJavaScriptDialog = "Page"
                                               ".handleJavaScriptDialog";
// events
constexpr const char *LoadEventFired = "Page"
                                       ".loadEventFired";
constexpr const char *FrameStoppedLoading = "Page"
                                            ".frameStoppedLoading";
constexpr const char *JavascriptDialogOpening = "Page"
                                                ".javascriptDialogOpening";
constexpr const char *JavascriptDialogClosed = "Page"
                                               ".javascriptDialogClosed";
} // namespace page

namespace runtime
{
constexpr const char *Enable = "Runtime"
                               ".enable";
constexpr const char *Evaluate = "Runtime"
                                 ".evaluate";
// events
constexpr const char *ConsoleAPICalled = "Runtime"
                                         ".consoleAPICalled";
constexpr const char *ExceptionThrown = "Runtime"
                                        ".exceptionThrown";
} // namespace runtime

namespace dom
{
constexpr const char *Enable = "DOM"
                               ".enable";
constexpr const char *DescribeNode = "DOM"
                                     ".describeNode";
constexpr const char *SetFileInputFiles = "DOM"
                                          ".setFileInputFiles";
} // namespace dom

namespace network
{
constexpr const char *Enable = "Network"
                               ".enable";
constexpr const char *GetCookies = "Network"
                                   ".getCookies";
constexpr const char *SetCookie = "Network"
                                  ".setCookie";
constexpr const char *ClearBrowserCookies = "Network"
                                            ".clearBrowserCookies";
constexpr const char *EmulateNetworkConditions = "Network"
                                                 ".emulateNetworkConditions";
constexpr const char *SetExtraHTTPHeaders = "Network"
                                            ".setExtraHTTPHeaders";
constexpr const char *GetResponseBody = "Network"
                                        ".getResponseBody";
// events
constexpr const char *RequestWillBeSent = "Network"
                                          ".requestWillBeSent";
constexpr const char *ResponseReceived = "Network"
                                         ".responseReceived";
constexpr const char *LoadingFinished = "Network"
                                        ".loadingFinished";
} // namespace network

namespace input
{
constexpr const char *DispatchMouseEvent = "Input"
                                           ".dispatchMouseEvent";
constexpr const char *DispatchKeyEvent = "Input"
                                         ".dispatchKeyEvent";
} // namespace input

namespace emulation
{
constexpr const char *SetDeviceMetricsOverride = "Emulation"
                                                 ".setDeviceMetricsOverride";
constexpr const char *ClearDeviceMetricsOverride = "Emulation"
                                                   ".clearDeviceMetricsOverride";
constexpr const char *SetGeolocationOverride = "Emulation"
                                               ".setGeolocationOverride";
constexpr const char *ClearGeolocationOverride = "Emulation"
                                                 ".clearGeolocationOverride";
constexpr const char *SetEmulatedMedia = "Emulation"
                                         ".setEmulatedMedia";
constexpr const char *SetTimezoneOverride = "Emulation"
                                            ".setTimezoneOverride";
constexpr const char *SetLocaleOverride = "Emulation"
                                          ".setLocaleOverride";
constexpr const char *SetUserAgentOverride = "Emulation"
                                             ".setUserAgentOverride";
} // namespace emulation

namespace fetch
{
constexpr const char *Enable = "Fetch"
                               ".enable";
constexpr const char *Disable = "Fetch"
                                ".disable";
constexpr const char *ContinueWithAuth = "Fetch"
                                         ".continueWithAuth";
constexpr const char *ContinueRequest = "Fetch"
                                        ".continueRequest";
// events
constexpr const char *AuthRequired = "Fetch"
                                     ".authRequired";
constexpr const char *RequestPaused = "Fetch"
                                      ".requestPaused";
} // namespace fetch

} // namespace cdp

// ── constants ──────────────────────────────────────────────────────────────

constexpr int CDP_PORT = 9222;
constexpr int CDP_TIMEOUT_SECONDS = 30;
constexpr int MAX_INTERACTIVE_ELEMENTS = 100;
constexpr size_t MAX_CONSOLE_MESSAGES = 500;
constexpr size_t MAX_PAGE_ERRORS = 200;
constexpr size_t MAX_NETWORK_REQUESTS = 500;
constexpr int MAX_SNAPSHOT_DEPTH = 15;
constexpr int MAX_SNAPSHOT_NODES = 500;
constexpr int MAX_WAIT_SECONDS = 60;
constexpr int SLOW_TYPE_DELAY_MS = 75;
constexpr int DEFAULT_SNAPSHOT_MAX_CHARS = 50000;
constexpr int MAX_TOOL_RESULT_CHARS = 100000; // safety cap for any tool result
constexpr int CHROME_LAUNCH_RETRIES = 30;
constexpr int CHROME_LAUNCH_RETRY_MS = 200;
constexpr int EVENT_DRAIN_TIMEOUT_MS = 100;
constexpr size_t WS_BUFFER_SIZE = 8388608; // 8MB — screenshots return large base64 payloads

// ── key definitions ────────────────────────────────────────────────────────

struct KeyInfo
{
    std::string key;
    std::string code;
    int keyCode;
};

// case-insensitive key lookup
static const std::unordered_map<std::string, KeyInfo> KEY_MAP = {
    {"enter", {"Enter", "Enter", 13}},
    {"tab", {"Tab", "Tab", 9}},
    {"escape", {"Escape", "Escape", 27}},
    {"backspace", {"Backspace", "Backspace", 8}},
    {"delete", {"Delete", "Delete", 46}},
    {"arrowup", {"ArrowUp", "ArrowUp", 38}},
    {"arrowdown", {"ArrowDown", "ArrowDown", 40}},
    {"arrowleft", {"ArrowLeft", "ArrowLeft", 37}},
    {"arrowright", {"ArrowRight", "ArrowRight", 39}},
    {"home", {"Home", "Home", 36}},
    {"end", {"End", "End", 35}},
    {"pageup", {"PageUp", "PageUp", 33}},
    {"pagedown", {"PageDown", "PageDown", 34}},
    {"space", {" ", "Space", 32}},
    {"insert", {"Insert", "Insert", 45}},
    {"f1", {"F1", "F1", 112}},
    {"f2", {"F2", "F2", 113}},
    {"f3", {"F3", "F3", 114}},
    {"f4", {"F4", "F4", 115}},
    {"f5", {"F5", "F5", 116}},
    {"f6", {"F6", "F6", 117}},
    {"f7", {"F7", "F7", 118}},
    {"f8", {"F8", "F8", 119}},
    {"f9", {"F9", "F9", 120}},
    {"f10", {"F10", "F10", 121}},
    {"f11", {"F11", "F11", 122}},
    {"f12", {"F12", "F12", 123}},
};

// ── device presets ─────────────────────────────────────────────────────────

struct DevicePreset
{
    int width;
    int height;
    double deviceScaleFactor;
    bool isMobile;
    std::string userAgent;
};

static const std::unordered_map<std::string, DevicePreset> DEVICE_PRESETS = {
    {"iphone 14", {390, 844, 3.0, true, "Mozilla/5.0 (iPhone; CPU iPhone OS 16_6 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/16.6 Mobile/15E148 Safari/604.1"}},
    {"iphone 14 pro max", {430, 932, 3.0, true, "Mozilla/5.0 (iPhone; CPU iPhone OS 16_6 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/16.6 Mobile/15E148 Safari/604.1"}},
    {"iphone se", {375, 667, 2.0, true, "Mozilla/5.0 (iPhone; CPU iPhone OS 16_6 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/16.6 Mobile/15E148 Safari/604.1"}},
    {"ipad", {768, 1024, 2.0, true, "Mozilla/5.0 (iPad; CPU OS 16_6 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/16.6 Mobile/15E148 Safari/604.1"}},
    {"ipad pro", {1024, 1366, 2.0, true, "Mozilla/5.0 (iPad; CPU OS 16_6 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/16.6 Mobile/15E148 Safari/604.1"}},
    {"pixel 7", {412, 915, 2.625, true, "Mozilla/5.0 (Linux; Android 13; Pixel 7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/116.0.0.0 Mobile Safari/537.36"}},
    {"samsung galaxy s23", {360, 780, 3.0, true, "Mozilla/5.0 (Linux; Android 13; SM-S911B) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/116.0.0.0 Mobile Safari/537.36"}},
    {"desktop 1080p", {1920, 1080, 1.0, false, ""}},
    {"desktop 1440p", {2560, 1440, 1.0, false, ""}},
};

// ── page state types ───────────────────────────────────────────────────────

struct ConsoleMessage
{
    std::string level;
    std::string text;
    double timestamp;
};

struct PageError
{
    std::string message;
    std::string url;
    int line;
    int column;
};

struct NetworkRequestEntry
{
    std::string requestId;
    std::string url;
    std::string method;
    int statusCode = 0;
    std::string mimeType;
    double timestamp = 0;
    bool finished = false;
};

struct ElementPos
{
    double x = 0;
    double y = 0;
    bool found = false;
};

// ── ChromeManager ──────────────────────────────────────────────────────────

class ChromeManager
{
public:
    static ChromeManager &instance()
    {
        static ChromeManager inst;
        return inst;
    }

    bool isRunning() const
    {
        if (pid_ <= 0)
        {
            return false;
        }

#ifndef _WIN32
        // check if process is actually alive
        if (kill(static_cast<pid_t>(pid_), 0) != 0)
        {
            return false;
        }
#endif

        return true;
    }

    std::string launch(bool headless)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (pid_ > 0)
        {
            // verify process is still alive
#ifndef _WIN32
            if (kill(static_cast<pid_t>(pid_), 0) == 0)
            {
                return "";
            }

            // process died, reset and re-launch
            spdlog::warn("browser: Chrome process {} is dead, re-launching", pid_);
            pid_ = 0;
#else
            return "";
#endif
        }

        auto chromePath = findChrome();

        if (chromePath.empty())
        {
            return "Error: Chrome/Chromium not found. Install Google Chrome, Brave, Edge, or Chromium to use the browser tool.";
        }

        std::string userDataDir;
        auto homeDir = std::getenv("HOME");

        if (homeDir)
        {
            userDataDir = std::string(homeDir) + "/.ionclaw/chrome-profile";
        }
        else
        {
#ifdef _WIN32
            userDataDir = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : "C:\\Temp") + "\\ionclaw-chrome-profile";
#else
            userDataDir = "/tmp/ionclaw-chrome-profile";
#endif
        }

        std::error_code ec;
        std::filesystem::create_directories(userDataDir, ec);

        std::ostringstream cmd;
        cmd << "\"" << chromePath << "\""
            << " --remote-debugging-port=" << CDP_PORT
            << " --user-data-dir=\"" << userDataDir << "\""
            << " --no-first-run"
            << " --no-service-autorun"
            << " --no-default-browser-check"
            << " --homepage=about:blank"
            << " --no-pings"
            << " --password-store=basic"
            << " --disable-infobars"
            << " --disable-breakpad"
            << " --disable-component-update"
            << " --disable-background-timer-throttling"
            << " --disable-backgrounding-occluded-windows"
            << " --disable-renderer-backgrounding"
            << " --disable-background-networking"
            << " --disable-dev-shm-usage"
            << " --disable-sync"
            << " --remote-allow-origins=*";

        if (headless)
        {
            cmd << " --headless=new --disable-gpu";
        }
        else
        {
            cmd << " --start-maximized";
        }

        cmd << " about:blank >/dev/null 2>&1";

#ifdef _WIN32
        auto cmdStr = cmd.str() + " &";
        auto result = std::system(cmdStr.c_str());

        if (result != 0)
        {
            return "Error: failed to launch Chrome";
        }

        pid_ = 1;
#elif defined(__ANDROID__)
        return "Error: browser tool is not supported on Android";
#else
        std::vector<std::string> args = {"/bin/sh", "-c", cmd.str()};
        std::vector<char *> argv;

        for (auto &a : args)
        {
            argv.push_back(a.data());
        }

        argv.push_back(nullptr);

        pid_t childPid = 0;
        auto spawnResult = posix_spawn(&childPid, "/bin/sh", nullptr, nullptr, argv.data(), environ);

        if (spawnResult != 0)
        {
            return "Error: failed to launch Chrome (spawn error " + std::to_string(spawnResult) + ")";
        }

        pid_ = childPid;
#endif

        for (int i = 0; i < CHROME_LAUNCH_RETRIES; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(CHROME_LAUNCH_RETRY_MS));

            try
            {
                auto response = ionclaw::util::HttpClient::request(
                    "GET", "http://127.0.0.1:" + std::to_string(CDP_PORT) + "/json", {}, "", 2, false);

                if (response.statusCode == 200)
                {
                    spdlog::info("browser: Chrome launched (pid={})", pid_);
                    return "";
                }
            }
            catch (const std::exception &e)
            {
                spdlog::debug("browser: CDP probe attempt {}/{} failed: {}", i + 1, CHROME_LAUNCH_RETRIES, e.what());
            }
        }

        return "Error: Chrome launched but CDP endpoint not responding on port " + std::to_string(CDP_PORT);
    }

    void shutdown()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (pid_ > 0)
        {
#ifdef _WIN32
            std::system("taskkill /F /IM chrome.exe > nul 2>&1");
#elif !defined(__ANDROID__)
            kill(static_cast<pid_t>(pid_), SIGTERM);
#endif
            pid_ = 0;
            spdlog::info("browser: Chrome shut down");
        }
    }

private:
    ChromeManager() = default;
    ~ChromeManager() { shutdown(); }

    std::string findChrome()
    {
#ifdef __APPLE__
        const char *paths[] = {
            "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
            "/Applications/Brave Browser.app/Contents/MacOS/Brave Browser",
            "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge",
            "/Applications/Chromium.app/Contents/MacOS/Chromium",
            "/Applications/Google Chrome Canary.app/Contents/MacOS/Google Chrome Canary",
        };

        for (auto path : paths)
        {
            if (std::ifstream(path).good())
            {
                return path;
            }
        }

        return "";
#elif defined(_WIN32)
        const char *paths[] = {
            "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
            "C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe",
            "C:\\Program Files\\BraveSoftware\\Brave-Browser\\Application\\brave.exe",
            "C:\\Program Files (x86)\\BraveSoftware\\Brave-Browser\\Application\\brave.exe",
            "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
            "C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe",
        };

        for (auto path : paths)
        {
            if (std::ifstream(path).good())
            {
                return path;
            }
        }

        return "";
#else
        const char *paths[] = {
            "/usr/bin/google-chrome",
            "/usr/bin/google-chrome-stable",
            "/usr/bin/brave-browser",
            "/usr/bin/brave-browser-stable",
            "/usr/bin/microsoft-edge",
            "/usr/bin/microsoft-edge-stable",
            "/usr/bin/chromium-browser",
            "/usr/bin/chromium",
            "/usr/local/bin/google-chrome",
            "/usr/local/bin/chromium",
        };

        for (auto path : paths)
        {
            if (std::ifstream(path).good())
            {
                return path;
            }
        }

        return "";
#endif
    }

    int pid_ = 0;
    std::mutex mutex_;
};

// ── CdpTab ─────────────────────────────────────────────────────────────────

class CdpTab
{
public:
    explicit CdpTab(const std::string &wsUrl)
        : wsUrl_(wsUrl)
        , recvBuffer_(WS_BUFFER_SIZE)
    {
    }

    ~CdpTab()
    {
        disconnect();
    }

    bool connect()
    {
        std::lock_guard<std::mutex> lock(wsMutex_);

        constexpr int maxRetries = 8;
        constexpr int retryDelayMs = 500;

        // parse the URL once - use explicit host/port to avoid scheme-dependent port mapping
        // (Poco maps ws:// to port 80 when port is not explicit)
        Poco::URI uri(wsUrl_);
        auto host = uri.getHost();
        auto port = static_cast<unsigned short>(CDP_PORT); // always use our known port
        auto path = uri.getPathAndQuery();

        if (path.empty())
        {
            path = "/";
        }

        spdlog::debug("browser: CDP connecting to {}:{}{} (from wsUrl={})", host, port, path, wsUrl_);

        for (int attempt = 0; attempt < maxRetries; ++attempt)
        {
            try
            {
                // reset any previous failed state
                ws_.reset();
                session_.reset();

                session_ = std::make_unique<Poco::Net::HTTPClientSession>(host, port);
                session_->setTimeout(Poco::Timespan(CDP_TIMEOUT_SECONDS, 0));

                Poco::Net::HTTPRequest request(
                    Poco::Net::HTTPRequest::HTTP_GET,
                    path,
                    Poco::Net::HTTPMessage::HTTP_1_1);
                request.set("Host", host + ":" + std::to_string(port));
                request.set("Origin", "http://" + host + ":" + std::to_string(port));

                Poco::Net::HTTPResponse response;
                ws_ = std::make_unique<Poco::Net::WebSocket>(*session_, request, response);
                ws_->setReceiveTimeout(Poco::Timespan(CDP_TIMEOUT_SECONDS, 0));

                connected_ = true;
                spdlog::info("browser: CDP connected to {}:{}{}", host, port, path);

                // enable required CDP domains
                sendCommandLocked(cdp::page::Enable);
                sendCommandLocked(cdp::runtime::Enable);
                sendCommandLocked(cdp::dom::Enable);
                sendCommandLocked(cdp::network::Enable);

                return true;
            }
            catch (const std::exception &e)
            {
                spdlog::warn("browser: CDP connect attempt {}/{} to {}:{} failed: {}",
                             attempt + 1, maxRetries, host, port, e.what());
                ws_.reset();
                session_.reset();

                if (attempt + 1 < maxRetries)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs * (attempt + 1)));
                }
            }
        }

        spdlog::error("browser: CDP connect to {}:{}{} failed after {} attempts", host, port, path, maxRetries);
        return false;
    }

    void disconnect()
    {
        std::lock_guard<std::mutex> lock(wsMutex_);
        connected_ = false;
        ws_.reset();
        session_.reset();
    }

    bool isConnected() const { return connected_; }

    nlohmann::json sendCommand(const std::string &method, const nlohmann::json &cmdParams = nlohmann::json::object())
    {
        std::lock_guard<std::mutex> lock(wsMutex_);
        return sendCommandLocked(method, cmdParams);
    }

    // drain pending events without sending a command
    void drainEvents()
    {
        std::lock_guard<std::mutex> lock(wsMutex_);

        if (!connected_ || !ws_)
        {
            return;
        }

        try
        {
            ws_->setReceiveTimeout(Poco::Timespan(0, EVENT_DRAIN_TIMEOUT_MS * 1000));
            int flags;

            while (true)
            {
                try
                {
                    auto n = ws_->receiveFrame(recvBuffer_.data(), static_cast<int>(recvBuffer_.size()), flags);

                    if (n <= 0)
                    {
                        break;
                    }

                    auto msg = nlohmann::json::parse(std::string(recvBuffer_.data(), n), nullptr, false);

                    if (msg.is_discarded())
                    {
                        continue;
                    }

                    if (!msg.contains("id"))
                    {
                        processEventLocked(msg);
                    }
                }
                catch (const Poco::TimeoutException &)
                {
                    break;
                }
            }

            ws_->setReceiveTimeout(Poco::Timespan(CDP_TIMEOUT_SECONDS, 0));
        }
        catch (const std::exception &e)
        {
            spdlog::debug("browser: drainEvents error: {}", e.what());
        }
    }

    bool waitForEvent(const std::string &eventName, int timeoutMs = 10000)
    {
        std::lock_guard<std::mutex> lock(wsMutex_);

        if (!connected_ || !ws_)
        {
            return false;
        }

        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        int flags;

        while (std::chrono::steady_clock::now() < deadline)
        {
            try
            {
                ws_->setReceiveTimeout(Poco::Timespan(0, 500000));

                auto n = ws_->receiveFrame(recvBuffer_.data(), static_cast<int>(recvBuffer_.size()), flags);

                if (n > 0)
                {
                    auto msg = nlohmann::json::parse(std::string(recvBuffer_.data(), n), nullptr, false);

                    if (msg.is_discarded())
                    {
                        continue;
                    }

                    if (!msg.contains("id"))
                    {
                        processEventLocked(msg);
                    }

                    if (msg.contains("method") && msg["method"].get<std::string>() == eventName)
                    {
                        ws_->setReceiveTimeout(Poco::Timespan(CDP_TIMEOUT_SECONDS, 0));
                        return true;
                    }
                }
            }
            catch (const Poco::TimeoutException &)
            {
            }
        }

        ws_->setReceiveTimeout(Poco::Timespan(CDP_TIMEOUT_SECONDS, 0));
        return false;
    }

    // page state accessors
    std::vector<ConsoleMessage> getConsole(bool clear = false)
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        auto result = console_;

        if (clear)
        {
            console_.clear();
        }

        return result;
    }

    std::vector<PageError> getErrors(bool clear = false)
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        auto result = errors_;

        if (clear)
        {
            errors_.clear();
        }

        return result;
    }

    std::vector<NetworkRequestEntry> getRequests(bool clear = false)
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        auto result = requests_;

        if (clear)
        {
            requests_.clear();
        }

        return result;
    }

    void clearConsole()
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        console_.clear();
    }

    void clearErrors()
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        errors_.clear();
    }

    void clearRequests()
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        requests_.clear();
    }

    bool hasPendingDialog() const { return hasPendingDialog_.load(); }

    nlohmann::json pendingDialogInfo()
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return dialogInfo_;
    }

    void clearPendingDialog() { hasPendingDialog_ = false; }

    void setAuthCredentials(const std::string &username, const std::string &password)
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        authUsername_ = username;
        authPassword_ = password;
    }

    void clearAuthCredentials()
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        authUsername_.clear();
        authPassword_.clear();
    }

    bool hasAuthCredentials()
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return !authUsername_.empty();
    }

private:
    nlohmann::json sendCommandLocked(const std::string &method, const nlohmann::json &cmdParams = nlohmann::json::object())
    {
        if (!connected_ || !ws_)
        {
            throw std::runtime_error("CDP not connected");
        }

        auto id = nextId_++;

        nlohmann::json message = {
            {"id", id},
            {"method", method},
            {"params", cmdParams},
        };

        auto msgStr = message.dump();
        ws_->sendFrame(msgStr.data(), static_cast<int>(msgStr.size()), Poco::Net::WebSocket::FRAME_TEXT);

        int flags;

        while (true)
        {
            std::string assembled;

            do
            {
                auto n = ws_->receiveFrame(recvBuffer_.data(), static_cast<int>(recvBuffer_.size()), flags);

                if (n <= 0)
                {
                    connected_ = false;
                    throw std::runtime_error("CDP connection closed unexpectedly");
                }

                // handle close frames
                if ((flags & Poco::Net::WebSocket::FRAME_OP_BITMASK) == Poco::Net::WebSocket::FRAME_OP_CLOSE)
                {
                    connected_ = false;
                    throw std::runtime_error("CDP connection closed by browser");
                }

                assembled.append(recvBuffer_.data(), static_cast<size_t>(n));
            } while (!(flags & Poco::Net::WebSocket::FRAME_FLAG_FIN));

            auto response = nlohmann::json::parse(assembled, nullptr, false);

            if (response.is_discarded())
            {
                continue;
            }

            // process events while waiting for our response
            if (!response.contains("id"))
            {
                processEventLocked(response);
                continue;
            }

            if (response["id"].get<int>() != id)
            {
                continue;
            }

            if (response.contains("error"))
            {
                auto errorMsg = response["error"].value("message", "unknown CDP error");
                throw std::runtime_error("CDP error: " + errorMsg);
            }

            return response.value("result", nlohmann::json::object());
        }
    }

    void processEventLocked(const nlohmann::json &event)
    {
        auto method = event.value("method", "");
        auto params = event.value("params", nlohmann::json::object());

        if (method == cdp::runtime::ConsoleAPICalled)
        {
            std::lock_guard<std::mutex> lock(stateMutex_);

            ConsoleMessage msg;
            msg.level = params.value("type", "log");
            msg.timestamp = params.value("timestamp", 0.0);

            std::ostringstream text;

            if (params.contains("args") && params["args"].is_array())
            {
                for (size_t i = 0; i < params["args"].size(); ++i)
                {
                    if (i > 0)
                    {
                        text << " ";
                    }

                    auto &arg = params["args"][i];

                    if (arg.contains("value"))
                    {
                        if (arg["value"].is_string())
                        {
                            text << arg["value"].get<std::string>();
                        }
                        else
                        {
                            text << arg["value"].dump();
                        }
                    }
                    else
                    {
                        text << arg.value("description", arg.value("type", ""));
                    }
                }
            }

            msg.text = text.str();
            console_.push_back(std::move(msg));

            if (console_.size() > MAX_CONSOLE_MESSAGES)
            {
                console_.erase(console_.begin());
            }
        }
        else if (method == cdp::runtime::ExceptionThrown)
        {
            std::lock_guard<std::mutex> lock(stateMutex_);

            PageError err;

            if (params.contains("exceptionDetails"))
            {
                auto &details = params["exceptionDetails"];
                err.line = details.value("lineNumber", 0);
                err.column = details.value("columnNumber", 0);
                err.url = details.value("url", "");

                if (details.contains("exception") && details["exception"].contains("description"))
                {
                    err.message = details["exception"]["description"].get<std::string>();
                }
                else
                {
                    err.message = details.value("text", "unknown error");
                }
            }

            errors_.push_back(std::move(err));

            if (errors_.size() > MAX_PAGE_ERRORS)
            {
                errors_.erase(errors_.begin());
            }
        }
        else if (method == cdp::network::RequestWillBeSent)
        {
            std::lock_guard<std::mutex> lock(stateMutex_);

            NetworkRequestEntry req;
            req.requestId = params.value("requestId", "");
            req.timestamp = params.value("timestamp", 0.0);

            if (params.contains("request"))
            {
                req.url = params["request"].value("url", "");
                req.method = params["request"].value("method", "GET");
            }

            requests_.push_back(std::move(req));

            if (requests_.size() > MAX_NETWORK_REQUESTS)
            {
                requests_.erase(requests_.begin());
            }
        }
        else if (method == cdp::network::ResponseReceived)
        {
            std::lock_guard<std::mutex> lock(stateMutex_);

            auto requestId = params.value("requestId", "");

            for (auto it = requests_.rbegin(); it != requests_.rend(); ++it)
            {
                if (it->requestId == requestId)
                {
                    if (params.contains("response"))
                    {
                        it->statusCode = params["response"].value("status", 0);
                        it->mimeType = params["response"].value("mimeType", "");
                    }

                    break;
                }
            }
        }
        else if (method == cdp::network::LoadingFinished)
        {
            std::lock_guard<std::mutex> lock(stateMutex_);

            auto requestId = params.value("requestId", "");

            for (auto it = requests_.rbegin(); it != requests_.rend(); ++it)
            {
                if (it->requestId == requestId)
                {
                    it->finished = true;
                    break;
                }
            }
        }
        else if (method == cdp::fetch::AuthRequired)
        {
            auto requestId = params.value("requestId", "");

            if (!requestId.empty())
            {
                std::string user, pass;
                {
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    user = authUsername_;
                    pass = authPassword_;
                }

                if (!user.empty())
                {
                    try
                    {
                        sendCommandLocked(cdp::fetch::ContinueWithAuth, {
                                                                            {"requestId", requestId},
                                                                            {"authChallengeResponse", {
                                                                                                          {"response", "ProvideCredentials"},
                                                                                                          {"username", user},
                                                                                                          {"password", pass},
                                                                                                      }},
                                                                        });
                    }
                    catch (const std::exception &e)
                    {
                        spdlog::warn("browser: failed to respond to auth challenge: {}", e.what());
                    }
                }
                else
                {
                    try
                    {
                        sendCommandLocked(cdp::fetch::ContinueWithAuth, {
                                                                            {"requestId", requestId},
                                                                            {"authChallengeResponse", {
                                                                                                          {"response", "CancelAuth"},
                                                                                                      }},
                                                                        });
                    }
                    catch (const std::exception &e)
                    {
                        spdlog::debug("browser: failed to cancel auth: {}", e.what());
                    }
                }
            }
        }
        else if (method == cdp::fetch::RequestPaused)
        {
            // when Fetch is enabled for auth, we need to continue non-auth requests
            auto requestId = params.value("requestId", "");

            if (!requestId.empty())
            {
                try
                {
                    sendCommandLocked(cdp::fetch::ContinueRequest, {{"requestId", requestId}});
                }
                catch (const std::exception &e)
                {
                    spdlog::debug("browser: failed to continue paused request: {}", e.what());
                }
            }
        }
        else if (method == cdp::page::JavascriptDialogOpening)
        {
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                dialogInfo_ = params;
            }
            hasPendingDialog_ = true;
        }
        else if (method == cdp::page::JavascriptDialogClosed)
        {
            hasPendingDialog_ = false;
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                dialogInfo_ = nlohmann::json::object();
            }
        }
    }

    std::string wsUrl_;
    std::unique_ptr<Poco::Net::HTTPClientSession> session_;
    std::unique_ptr<Poco::Net::WebSocket> ws_;
    std::atomic<bool> connected_{false};
    std::atomic<int> nextId_{1};
    std::mutex wsMutex_;
    std::vector<char> recvBuffer_; // reusable receive buffer (allocated once per tab)

    std::vector<ConsoleMessage> console_;
    std::vector<PageError> errors_;
    std::vector<NetworkRequestEntry> requests_;
    std::atomic<bool> hasPendingDialog_{false};
    nlohmann::json dialogInfo_;
    std::string authUsername_;
    std::string authPassword_;
    std::mutex stateMutex_; // protects console_, errors_, requests_, dialogInfo_, auth*
};

// ── TabManager ─────────────────────────────────────────────────────────────

class TabManager
{
public:
    static TabManager &instance()
    {
        static TabManager inst;
        return inst;
    }

    nlohmann::json listTabs()
    {
        try
        {
            auto response = ionclaw::util::HttpClient::request(
                "GET", "http://127.0.0.1:" + std::to_string(CDP_PORT) + "/json", {}, "", 5, false);

            if (response.statusCode != 200)
            {
                spdlog::warn("browser: /json endpoint returned status {}", response.statusCode);
                return nlohmann::json::array();
            }

            auto targets = nlohmann::json::parse(response.body, nullptr, false);

            if (targets.is_discarded())
            {
                spdlog::warn("browser: /json returned invalid JSON");
                return nlohmann::json::array();
            }

            nlohmann::json pages = nlohmann::json::array();

            for (const auto &target : targets)
            {
                if (target.value("type", "") == "page")
                {
                    pages.push_back({
                        {"targetId", target.value("id", "")},
                        {"url", target.value("url", "")},
                        {"title", target.value("title", "")},
                    });
                }
            }

            return pages;
        }
        catch (const std::exception &e)
        {
            spdlog::warn("browser: failed to list tabs: {}", e.what());
            return nlohmann::json::array();
        }
    }

    std::string openTab(const std::string &url = "about:blank")
    {
        try
        {
            auto response = ionclaw::util::HttpClient::request(
                "GET", "http://127.0.0.1:" + std::to_string(CDP_PORT) + "/json/new?" + url, {}, "", 5, false);

            if (response.statusCode != 200)
            {
                spdlog::warn("browser: /json/new returned status {}", response.statusCode);
                return "";
            }

            auto result = nlohmann::json::parse(response.body, nullptr, false);

            if (result.is_discarded())
            {
                spdlog::warn("browser: /json/new returned invalid JSON");
                return "";
            }

            auto targetId = result.value("id", "");

            if (!targetId.empty())
            {
                std::lock_guard<std::mutex> lock(mutex_);
                currentTargetId_ = targetId;
            }

            return targetId;
        }
        catch (const std::exception &e)
        {
            spdlog::warn("browser: failed to open tab: {}", e.what());
            return "";
        }
    }

    bool focusTab(const std::string &targetId)
    {
        try
        {
            auto response = ionclaw::util::HttpClient::request(
                "GET", "http://127.0.0.1:" + std::to_string(CDP_PORT) + "/json/activate/" + targetId, {}, "", 5, false);

            if (response.statusCode == 200)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                currentTargetId_ = targetId;
                return true;
            }

            spdlog::warn("browser: /json/activate returned status {}", response.statusCode);
            return false;
        }
        catch (const std::exception &e)
        {
            spdlog::warn("browser: failed to focus tab {}: {}", targetId, e.what());
            return false;
        }
    }

    bool closeTab(const std::string &targetId)
    {
        auto id = targetId;

        if (id.empty())
        {
            std::lock_guard<std::mutex> lock(mutex_);
            id = currentTargetId_;
        }

        if (id.empty())
        {
            return false;
        }

        try
        {
            // disconnect CdpTab if we have one
            {
                std::lock_guard<std::mutex> lock(mutex_);
                tabs_.erase(id);

                if (currentTargetId_ == id)
                {
                    currentTargetId_.clear();
                }
            }

            auto response = ionclaw::util::HttpClient::request(
                "GET", "http://127.0.0.1:" + std::to_string(CDP_PORT) + "/json/close/" + id, {}, "", 5, false);

            // set current to another available tab
            {
                std::lock_guard<std::mutex> lock(mutex_);

                if (currentTargetId_.empty())
                {
                    auto tabs = listTabs();

                    if (!tabs.empty())
                    {
                        currentTargetId_ = tabs[0].value("targetId", "");
                    }
                }
            }

            return response.statusCode == 200;
        }
        catch (const std::exception &e)
        {
            spdlog::warn("browser: failed to close tab {}: {}", id, e.what());
            return false;
        }
    }

    CdpTab *currentTab()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // auto-select current tab if not set
        if (currentTargetId_.empty())
        {
            auto tabs = listTabs();

            for (const auto &tab : tabs)
            {
                auto id = tab.value("targetId", "");

                if (!id.empty())
                {
                    currentTargetId_ = id;
                    break;
                }
            }
        }

        if (currentTargetId_.empty())
        {
            return nullptr;
        }

        return getOrConnectTabLocked(currentTargetId_);
    }

    CdpTab *getTab(const std::string &targetId)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return getOrConnectTabLocked(targetId);
    }

    void disconnectAll()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tabs_.clear();
        currentTargetId_.clear();
    }

    std::string currentTargetId()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return currentTargetId_;
    }

private:
    TabManager() = default;

    ~TabManager()
    {
        disconnectAll();
    }

    CdpTab *getOrConnectTabLocked(const std::string &targetId)
    {
        auto it = tabs_.find(targetId);

        if (it != tabs_.end() && it->second->isConnected())
        {
            return it->second.get();
        }

        // verify the target exists via /json before connecting
        bool targetFound = false;

        try
        {
            auto response = ionclaw::util::HttpClient::request(
                "GET", "http://127.0.0.1:" + std::to_string(CDP_PORT) + "/json", {}, "", 5, false);

            if (response.statusCode == 200)
            {
                auto targets = nlohmann::json::parse(response.body, nullptr, false);

                if (!targets.is_discarded())
                {
                    for (const auto &target : targets)
                    {
                        if (target.value("id", "") == targetId)
                        {
                            targetFound = true;
                            spdlog::debug("browser: found target {} (url={}, type={})",
                                          targetId,
                                          target.value("url", ""),
                                          target.value("type", ""));
                            break;
                        }
                    }
                }
            }
        }
        catch (const std::exception &e)
        {
            spdlog::warn("browser: failed to query /json for target {}: {}", targetId, e.what());
            return nullptr;
        }

        if (!targetFound)
        {
            spdlog::warn("browser: target {} not found in /json response", targetId);
            return nullptr;
        }

        // construct WebSocket URL deterministically (don't rely on webSocketDebuggerUrl
        // which may be absent in newer Chrome versions or have wrong host/port)
        auto wsUrl = "ws://127.0.0.1:" + std::to_string(CDP_PORT) + "/devtools/page/" + targetId;
        spdlog::debug("browser: connecting to {}", wsUrl);

        auto tab = std::make_unique<CdpTab>(wsUrl);

        if (!tab->connect())
        {
            return nullptr;
        }

        auto *ptr = tab.get();
        tabs_[targetId] = std::move(tab);
        return ptr;
    }

    std::map<std::string, std::unique_ptr<CdpTab>> tabs_;
    std::string currentTargetId_;
    std::mutex mutex_;
};

// ── action helpers ─────────────────────────────────────────────────────────

std::string ensureBrowser(bool headless)
{
    auto &chrome = ChromeManager::instance();

    if (!chrome.isRunning())
    {
        auto err = chrome.launch(headless);

        if (!err.empty())
        {
            return err;
        }
    }

    return "";
}

struct TabResult
{
    CdpTab *tab = nullptr;
    std::string error;
};

TabResult ensureTab(bool headless)
{
    auto err = ensureBrowser(headless);

    if (!err.empty())
    {
        return {nullptr, err};
    }

    auto *tab = TabManager::instance().currentTab();

    if (!tab)
    {
        return {nullptr, "Error: failed to connect to browser tab via CDP. "
                         "The browser may have crashed or the tab may be unresponsive. "
                         "Try action='stop' then action='start' to restart."};
    }

    return {tab, ""};
}

// safe accessor for CDP evaluate results — avoids throws on missing "result" key
nlohmann::json evalResult(const nlohmann::json &cdpResponse)
{
    return cdpResponse.value("result", nlohmann::json::object());
}

// ── page state helpers ────────────────────────────────────────────────────

struct PageInfo
{
    std::string url;
    std::string title;
};

PageInfo getPageInfo(CdpTab &tab)
{
    PageInfo info;
    auto result = tab.sendCommand(cdp::runtime::Evaluate, {
                                                              {"expression", "JSON.stringify({u:location.href,t:document.title})"},
                                                              {"returnByValue", true},
                                                          });
    auto json = nlohmann::json::parse(evalResult(result).value("value", "{}"), nullptr, false);

    if (!json.is_discarded())
    {
        info.url = json.value("u", "");
        info.title = json.value("t", "");
    }

    return info;
}

std::string formatTabContext()
{
    auto tabs = TabManager::instance().listTabs();
    auto currentId = TabManager::instance().currentTargetId();
    int currentIdx = 0;

    for (int i = 0; i < static_cast<int>(tabs.size()); i++)
    {
        if (tabs[i].value("targetId", "") == currentId)
        {
            currentIdx = i + 1;
            break;
        }
    }

    return "(tab " + std::to_string(currentIdx) + "/" + std::to_string(tabs.size()) + ")";
}

std::string formatPageState(CdpTab &tab)
{
    PageInfo info;

    try
    {
        info = getPageInfo(tab);
    }
    catch (const std::exception &e)
    {
        spdlog::warn("browser: failed to get page info: {}", e.what());
    }

    auto ctx = formatTabContext();
    std::ostringstream out;
    out << " [" << (info.url.empty() ? "(unknown)" : info.url);

    if (!info.title.empty())
    {
        out << " | \"" << info.title << "\"";
    }

    out << " | " << ctx << "]";
    return out.str();
}

ElementPos getElementCenter(CdpTab &tab, const std::string &selector)
{
    auto safeSelector = ToolHelper::escapeForJs(selector);

    auto result = tab.sendCommand(cdp::runtime::Evaluate, {
                                                              {"expression",
                                                               "(() => { const el = document.querySelector('" + safeSelector + "');"
                                                                                                                               "if (!el) return null;"
                                                                                                                               "if (el.scrollIntoViewIfNeeded) el.scrollIntoViewIfNeeded(true);"
                                                                                                                               "else el.scrollIntoView({block:'center',inline:'center'});"
                                                                                                                               "const r = el.getBoundingClientRect();"
                                                                                                                               "return {x: r.x + r.width/2, y: r.y + r.height/2}; })()"},
                                                              {"returnByValue", true},
                                                          });

    auto value = evalResult(result).value("value", nlohmann::json());

    if (value.is_null() || !value.contains("x") || !value["x"].is_number())
    {
        return {0, 0, false};
    }

    return {value["x"].get<double>(), value["y"].get<double>(), true};
}

void performClick(CdpTab &tab, double x, double y, const std::string &button = "left", bool doubleClick = false)
{
    auto cdpButton = (button == "right") ? "right" : ((button == "middle") ? "middle" : "left");
    int buttons = (button == "right") ? 2 : ((button == "middle") ? 4 : 1);

    tab.sendCommand(cdp::input::DispatchMouseEvent, {
                                                        {"type", "mouseMoved"},
                                                        {"x", x},
                                                        {"y", y},
                                                    });

    tab.sendCommand(cdp::input::DispatchMouseEvent, {
                                                        {"type", "mousePressed"},
                                                        {"x", x},
                                                        {"y", y},
                                                        {"button", cdpButton},
                                                        {"clickCount", 1},
                                                        {"buttons", buttons},
                                                        {"pointerType", "mouse"},
                                                    });

    tab.sendCommand(cdp::input::DispatchMouseEvent, {
                                                        {"type", "mouseReleased"},
                                                        {"x", x},
                                                        {"y", y},
                                                        {"button", cdpButton},
                                                        {"clickCount", 1},
                                                        {"pointerType", "mouse"},
                                                    });

    if (doubleClick)
    {
        tab.sendCommand(cdp::input::DispatchMouseEvent, {
                                                            {"type", "mousePressed"},
                                                            {"x", x},
                                                            {"y", y},
                                                            {"button", cdpButton},
                                                            {"clickCount", 2},
                                                            {"buttons", buttons},
                                                            {"pointerType", "mouse"},
                                                        });

        tab.sendCommand(cdp::input::DispatchMouseEvent, {
                                                            {"type", "mouseReleased"},
                                                            {"x", x},
                                                            {"y", y},
                                                            {"button", cdpButton},
                                                            {"clickCount", 2},
                                                            {"pointerType", "mouse"},
                                                        });
    }
}

void typeCharacters(CdpTab &tab, const std::string &text, bool slowly = false)
{
    for (char c : text)
    {
        std::string ch(1, c);

        tab.sendCommand(cdp::input::DispatchKeyEvent, {
                                                          {"type", "keyDown"},
                                                          {"text", ch},
                                                          {"key", ch},
                                                      });

        tab.sendCommand(cdp::input::DispatchKeyEvent, {
                                                          {"type", "keyUp"},
                                                          {"key", ch},
                                                      });

        if (slowly)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(SLOW_TYPE_DELAY_MS));
        }
    }
}

std::string pressKey(CdpTab &tab, const std::string &keyName)
{
    std::string lower = keyName;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c)
                   { return c < 0x80 ? std::tolower(c) : c; });

    auto it = KEY_MAP.find(lower);

    if (it == KEY_MAP.end())
    {
        return "Error: unknown key '" + keyName + "'. Available: Enter, Tab, Escape, Backspace, Delete, "
                                                  "ArrowUp, ArrowDown, ArrowLeft, ArrowRight, Home, End, PageUp, PageDown, Space, "
                                                  "Insert, F1-F12";
    }

    auto &info = it->second;

    tab.sendCommand(cdp::input::DispatchKeyEvent, {
                                                      {"type", "keyDown"},
                                                      {"key", info.key},
                                                      {"code", info.code},
                                                      {"windowsVirtualKeyCode", info.keyCode},
                                                      {"nativeVirtualKeyCode", info.keyCode},
                                                  });

    tab.sendCommand(cdp::input::DispatchKeyEvent, {
                                                      {"type", "keyUp"},
                                                      {"key", info.key},
                                                      {"code", info.code},
                                                      {"windowsVirtualKeyCode", info.keyCode},
                                                      {"nativeVirtualKeyCode", info.keyCode},
                                                  });

    return "";
}

bool matchGlob(const std::string &text, const std::string &pattern)
{
    size_t ti = 0, pi = 0;
    size_t starTi = std::string::npos, starPi = std::string::npos;

    while (ti < text.size())
    {
        if (pi < pattern.size() && (pattern[pi] == text[ti] || pattern[pi] == '?'))
        {
            ti++;
            pi++;
        }
        else if (pi < pattern.size() && pattern[pi] == '*')
        {
            starPi = pi++;
            starTi = ti;
        }
        else if (starPi != std::string::npos)
        {
            pi = starPi + 1;
            ti = ++starTi;
        }
        else
        {
            return false;
        }
    }

    while (pi < pattern.size() && pattern[pi] == '*')
    {
        pi++;
    }

    return pi == pattern.size();
}

// ── base64 helpers ─────────────────────────────────────────────────────────

static const std::string B64_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::vector<unsigned char> decodeBase64(const std::string &encoded)
{
    std::vector<unsigned char> result;
    result.reserve(encoded.size() * 3 / 4);

    std::array<unsigned char, 4> buf4{};
    int collected = 0;

    for (char c : encoded)
    {
        if (c == '=' || c == '\n' || c == '\r')
            continue;
        auto pos = B64_CHARS.find(c);
        if (pos == std::string::npos)
            continue;
        buf4[collected++] = static_cast<unsigned char>(pos);

        if (collected == 4)
        {
            result.push_back(static_cast<unsigned char>((buf4[0] << 2) | (buf4[1] >> 4)));
            result.push_back(static_cast<unsigned char>(((buf4[1] & 0x0F) << 4) | (buf4[2] >> 2)));
            result.push_back(static_cast<unsigned char>(((buf4[2] & 0x03) << 6) | buf4[3]));
            collected = 0;
        }
    }

    if (collected == 3)
    {
        result.push_back(static_cast<unsigned char>((buf4[0] << 2) | (buf4[1] >> 4)));
        result.push_back(static_cast<unsigned char>(((buf4[1] & 0x0F) << 4) | (buf4[2] >> 2)));
    }
    else if (collected == 2)
    {
        result.push_back(static_cast<unsigned char>((buf4[0] << 2) | (buf4[1] >> 4)));
    }

    return result;
}

std::string encodeBase64(const unsigned char *data, size_t len)
{
    std::string result;
    result.reserve((len + 2) / 3 * 4);

    for (size_t i = 0; i < len; i += 3)
    {
        unsigned int b = (static_cast<unsigned int>(data[i]) << 16);
        if (i + 1 < len)
            b |= (static_cast<unsigned int>(data[i + 1]) << 8);
        if (i + 2 < len)
            b |= static_cast<unsigned int>(data[i + 2]);

        result.push_back(B64_CHARS[(b >> 18) & 0x3F]);
        result.push_back(B64_CHARS[(b >> 12) & 0x3F]);
        result.push_back((i + 1 < len) ? B64_CHARS[(b >> 6) & 0x3F] : '=');
        result.push_back((i + 2 < len) ? B64_CHARS[b & 0x3F] : '=');
    }

    return result;
}

std::filesystem::path browserTempDir()
{
    auto dir = std::filesystem::temp_directory_path() / "ionclaw" / "browser";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::string tempFileName(const std::string &prefix, const std::string &ext)
{
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return prefix + "_" + std::to_string(ms) + ext;
}

bool saveToFile(const std::filesystem::path &path, const unsigned char *data, size_t size)
{
    std::ofstream out(path, std::ios::binary);

    if (!out.good())
    {
        return false;
    }

    out.write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(size));
    out.flush();
    return out.good();
}

#ifdef IONCLAW_HAS_STB_IMAGE_WRITE
// stb callback that appends to a vector<unsigned char>
void stbWriteToVector(void *context, void *data, int size)
{
    auto *vec = static_cast<std::vector<unsigned char> *>(context);
    auto *bytes = static_cast<unsigned char *>(data);
    vec->insert(vec->end(), bytes, bytes + size);
}
#endif

std::string capResult(std::string text, int maxChars = MAX_TOOL_RESULT_CHARS)
{
    if (static_cast<int>(text.size()) > maxChars)
    {
        text = ionclaw::util::StringHelper::utf8SafeTruncate(text, maxChars) + "\n... [truncated at " +
               std::to_string(maxChars / 1024) + "KB]";
    }

    return text;
}

// ── action implementations ─────────────────────────────────────────────────

std::string actionStatus()
{
    auto &chrome = ChromeManager::instance();

    if (!chrome.isRunning())
    {
        return "Browser is not running. Use action='start' to launch.";
    }

    auto tabs = TabManager::instance().listTabs();
    return "Browser is running. " + std::to_string(tabs.size()) + " tab(s) open.";
}

std::string actionStart(bool headless)
{
    auto err = ensureBrowser(headless);

    if (!err.empty())
    {
        return err;
    }

    auto tabs = TabManager::instance().listTabs();
    std::string currentUrl;

    if (!tabs.empty())
    {
        currentUrl = tabs[0].value("url", "");
    }

    return "Browser started with " + std::to_string(tabs.size()) + " tab(s)." +
           (currentUrl.empty() ? "" : " Current page: " + currentUrl) +
           " Use action='navigate' with a URL to go to a page.";
}

std::string actionStop()
{
    TabManager::instance().disconnectAll();
    ChromeManager::instance().shutdown();
    return "Browser stopped.";
}

std::string actionTabs()
{
    auto tabs = TabManager::instance().listTabs();

    if (tabs.empty())
    {
        return "No tabs open.";
    }

    auto currentId = TabManager::instance().currentTargetId();
    std::ostringstream out;
    out << "Open tabs (" << tabs.size() << "):\n\n";

    for (const auto &tab : tabs)
    {
        auto id = tab.value("targetId", "");
        auto isCurrent = (id == currentId) ? " [current]" : "";
        out << "  " << tab.value("title", "(untitled)")
            << "  " << tab.value("url", "")
            << "  targetId=" << id
            << isCurrent << "\n";
    }

    return out.str();
}

std::string actionOpen(bool headless, const std::string &url)
{
    auto err = ensureBrowser(headless);

    if (!err.empty())
    {
        return err;
    }

    auto targetId = TabManager::instance().openTab(url);

    if (targetId.empty())
    {
        return "Error: failed to open new tab. The browser may not be responding to CDP requests. "
               "Try action='stop' then action='start' to restart the browser.";
    }

    auto tabs = TabManager::instance().listTabs();
    return "Opened new tab (targetId=" + targetId + "). " +
           "Now " + std::to_string(tabs.size()) + " tab(s) open. " +
           "NOTE: If you just want to go to a URL, use action='navigate' instead - "
           "it uses the current tab without opening new ones.";
}

std::string actionFocus(const std::string &targetId)
{
    if (targetId.empty())
    {
        return "Error: 'target_id' is required for focus action";
    }

    if (!TabManager::instance().focusTab(targetId))
    {
        return "Error: tab not found: " + targetId;
    }

    // get URL of newly focused tab
    auto *tab = TabManager::instance().getTab(targetId);

    if (tab && tab->isConnected())
    {
        return "Focused tab: " + targetId + formatPageState(*tab);
    }

    return "Focused tab: " + targetId + " " + formatTabContext();
}

std::string actionClose(const std::string &targetId)
{
    if (!TabManager::instance().closeTab(targetId))
    {
        return "Error: failed to close tab. It may have already been closed or the browser is not responding.";
    }

    auto tabs = TabManager::instance().listTabs();

    if (tabs.empty())
    {
        return "Tab closed. No tabs remaining.";
    }

    auto currentId = TabManager::instance().currentTargetId();
    std::string currentUrl;

    for (const auto &t : tabs)
    {
        if (t.value("targetId", "") == currentId)
        {
            currentUrl = t.value("url", "");
            break;
        }
    }

    return "Tab closed. " + std::to_string(tabs.size()) + " tab(s) remaining. " +
           "Current tab: " + (currentUrl.empty() ? "(unknown)" : currentUrl);
}

std::string actionNavigate(CdpTab &tab, const std::string &url)
{
    if (url.empty())
    {
        return "Error: 'url' is required for navigate action";
    }

    // drain any stale events from previous operations
    tab.drainEvents();

    auto navResult = tab.sendCommand(cdp::page::Navigate, {{"url", url}});

    // check for navigation errors (DNS failure, connection refused, etc.)
    auto errorText = navResult.value("errorText", "");

    if (!errorText.empty())
    {
        return "Error: navigation to " + url + " failed: " + errorText +
               " " + formatTabContext();
    }

    bool loaded = tab.waitForEvent(cdp::page::LoadEventFired, 30000);

    // get actual page state after navigation
    PageInfo pageInfo;

    try
    {
        pageInfo = getPageInfo(tab);
    }
    catch (const std::exception &e)
    {
        spdlog::warn("browser: failed to get page info after navigation: {}", e.what());
    }

    auto ctx = formatTabContext();

    if (!loaded)
    {
        return "Warning: page load timed out (30s). "
               "Current URL: " +
               (pageInfo.url.empty() ? "(unknown)" : pageInfo.url) +
               (pageInfo.title.empty() ? "" : " | Title: \"" + pageInfo.title + "\"") +
               " " + ctx + ". The page may still be loading - use action='snapshot' to check.";
    }

    std::ostringstream out;
    out << "Navigated to: " << (pageInfo.url.empty() ? url : pageInfo.url);

    if (!pageInfo.title.empty())
    {
        out << " | Title: \"" << pageInfo.title << "\"";
    }

    if (!pageInfo.url.empty() && pageInfo.url != url)
    {
        out << " (redirected from " << url << ")";
    }

    out << " " << ctx;
    return out.str();
}

std::string actionBack(CdpTab &tab)
{
    auto historyResult = tab.sendCommand(cdp::page::GetNavigationHistory);
    int currentIndex = historyResult.value("currentIndex", 0);

    if (currentIndex <= 0)
    {
        return "Error: no previous page in history." + formatPageState(tab);
    }

    auto entries = historyResult.value("entries", nlohmann::json::array());

    if (currentIndex - 1 >= static_cast<int>(entries.size()))
    {
        return "Error: navigation history is corrupted (index out of bounds)." + formatPageState(tab);
    }

    int entryId = entries[currentIndex - 1].value("id", 0);

    tab.sendCommand(cdp::page::NavigateToHistoryEntry, {{"entryId", entryId}});
    tab.waitForEvent(cdp::page::LoadEventFired, 15000);

    PageInfo info;

    try
    {
        info = getPageInfo(tab);
    }
    catch (const std::exception &e)
    {
        spdlog::warn("browser: failed to get page info after back: {}", e.what());
    }

    return "Navigated back to: " + (info.url.empty() ? "(unknown)" : info.url) +
           (info.title.empty() ? "" : " | Title: \"" + info.title + "\"") +
           " " + formatTabContext();
}

std::string actionForward(CdpTab &tab)
{
    auto historyResult = tab.sendCommand(cdp::page::GetNavigationHistory);
    int currentIndex = historyResult.value("currentIndex", 0);
    auto entries = historyResult.value("entries", nlohmann::json::array());

    if (currentIndex + 1 >= static_cast<int>(entries.size()))
    {
        return "Error: no next page in history." + formatPageState(tab);
    }

    int entryId = entries[currentIndex + 1].value("id", 0);

    tab.sendCommand(cdp::page::NavigateToHistoryEntry, {{"entryId", entryId}});
    tab.waitForEvent(cdp::page::LoadEventFired, 15000);

    PageInfo info;

    try
    {
        info = getPageInfo(tab);
    }
    catch (const std::exception &e)
    {
        spdlog::warn("browser: failed to get page info after forward: {}", e.what());
    }

    return "Navigated forward to: " + (info.url.empty() ? "(unknown)" : info.url) +
           (info.title.empty() ? "" : " | Title: \"" + info.title + "\"") +
           " " + formatTabContext();
}

std::string actionReload(CdpTab &tab)
{
    tab.sendCommand(cdp::page::Reload);
    tab.waitForEvent(cdp::page::LoadEventFired, 30000);

    PageInfo info;

    try
    {
        info = getPageInfo(tab);
    }
    catch (const std::exception &e)
    {
        spdlog::warn("browser: failed to get page info after reload: {}", e.what());
    }

    return "Reloaded: " + (info.url.empty() ? "(unknown)" : info.url) +
           (info.title.empty() ? "" : " | Title: \"" + info.title + "\"") +
           " " + formatTabContext();
}

std::string actionScroll(CdpTab &tab, const nlohmann::json &params)
{
    auto direction = params.value("direction", "down");
    int amount = params.value("amount", 0);

    // default: one viewport height
    if (amount == 0)
    {
        auto result = tab.sendCommand(cdp::runtime::Evaluate, {
                                                                  {"expression", "window.innerHeight"},
                                                                  {"returnByValue", true},
                                                              });
        amount = evalResult(result).value("value", 600);
    }

    int scrollX = 0;
    int scrollY = 0;

    if (direction == "down")
        scrollY = amount;
    else if (direction == "up")
        scrollY = -amount;
    else if (direction == "right")
        scrollX = amount;
    else if (direction == "left")
        scrollX = -amount;
    else
        return "Error: unknown direction '" + direction + "'. Use: up, down, left, right";

    tab.sendCommand(cdp::runtime::Evaluate, {
                                                {"expression", "window.scrollBy(" + std::to_string(scrollX) + "," + std::to_string(scrollY) + ")"},
                                                {"returnByValue", true},
                                            });

    auto posResult = tab.sendCommand(cdp::runtime::Evaluate, {
                                                                 {"expression", "JSON.stringify({x:Math.round(scrollX),y:Math.round(scrollY),"
                                                                                "w:document.body.scrollWidth,h:document.body.scrollHeight,"
                                                                                "vw:window.innerWidth,vh:window.innerHeight})"},
                                                                 {"returnByValue", true},
                                                             });
    auto pos = nlohmann::json::parse(evalResult(posResult).value("value", "{}"), nullptr, false);

    bool isHorizontal = (direction == "left" || direction == "right");
    int scrollPos = pos.value(isHorizontal ? "x" : "y", 0);
    int totalSize = pos.value(isHorizontal ? "w" : "h", 0);
    int viewportSize = pos.value(isHorizontal ? "vw" : "vh", 0);
    int scrolledAmount = std::abs(isHorizontal ? scrollX : scrollY);

    return "Scrolled " + direction + " " + std::to_string(scrolledAmount) + "px. " +
           "Position: " + std::to_string(scrollPos) + "/" + std::to_string(totalSize) + "px" +
           (totalSize > 0 && viewportSize > 0
                ? " (" + std::to_string(std::min(100, (scrollPos + viewportSize) * 100 / totalSize)) + "% visible)"
                : "");
}

std::string actionWait(CdpTab *tab, const nlohmann::json &params)
{
    int seconds = std::max(1, std::min(MAX_WAIT_SECONDS, params.value("seconds", 2)));

    auto waitText = params.value("text", "");
    auto waitSelector = params.value("wait_selector", "");
    auto waitUrl = params.value("wait_url", "");
    auto waitFn = params.value("wait_fn", "");

    // simple time-only wait
    if (waitText.empty() && waitSelector.empty() && waitUrl.empty() && waitFn.empty())
    {
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
        return "Waited " + std::to_string(seconds) + " seconds.";
    }

    if (!tab)
    {
        return "Error: no active tab for wait conditions";
    }

    int timeoutMs = seconds * 1000;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    // track which conditions are still pending for error reporting
    bool textMet = waitText.empty();
    bool selectorMet = waitSelector.empty();
    bool urlMet = waitUrl.empty();
    bool fnMet = waitFn.empty();

    while (std::chrono::steady_clock::now() < deadline)
    {
        // check if tab disconnected — fail fast instead of looping on broken connection
        if (!tab->isConnected())
        {
            return "Error: browser tab disconnected while waiting. Try action='stop' then action='start'.";
        }

        try
        {
            if (!textMet)
            {
                auto result = tab->sendCommand(cdp::runtime::Evaluate, {
                                                                           {"expression", "document.body.innerText"},
                                                                           {"returnByValue", true},
                                                                       });
                textMet = evalResult(result).value("value", "").find(waitText) != std::string::npos;
            }

            if (!selectorMet)
            {
                auto safeSelector = ToolHelper::escapeForJs(waitSelector);
                auto result = tab->sendCommand(cdp::runtime::Evaluate, {
                                                                           {"expression", "!!document.querySelector('" + safeSelector + "')"},
                                                                           {"returnByValue", true},
                                                                       });
                selectorMet = evalResult(result).value("value", false);
            }

            if (!urlMet)
            {
                auto result = tab->sendCommand(cdp::runtime::Evaluate, {
                                                                           {"expression", "window.location.href"},
                                                                           {"returnByValue", true},
                                                                       });
                urlMet = matchGlob(evalResult(result).value("value", ""), waitUrl);
            }

            if (!fnMet)
            {
                auto result = tab->sendCommand(cdp::runtime::Evaluate, {
                                                                           {"expression", "!!(" + waitFn + ")"},
                                                                           {"returnByValue", true},
                                                                       });
                fnMet = evalResult(result).value("value", false);
            }
        }
        catch (const std::exception &e)
        {
            // connection failure — fail fast
            return "Error: wait failed due to CDP error: " + std::string(e.what());
        }

        if (textMet && selectorMet && urlMet && fnMet)
        {
            return "Wait conditions met.";
        }

        // reset all conditions between polls (any can regress if DOM changes dynamically)
        if (!waitText.empty())
            textMet = false;
        if (!waitSelector.empty())
            selectorMet = false;
        if (!waitUrl.empty())
            urlMet = false;
        if (!waitFn.empty())
            fnMet = false;

        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    // build descriptive timeout message
    std::ostringstream failMsg;
    failMsg << "Error: wait timed out after " << seconds << "s. Conditions not met:";

    if (!waitText.empty() && !textMet)
        failMsg << " text='" << waitText << "'";
    if (!waitSelector.empty() && !selectorMet)
        failMsg << " selector='" << waitSelector << "'";
    if (!waitUrl.empty() && !urlMet)
        failMsg << " url='" << waitUrl << "'";
    if (!waitFn.empty() && !fnMet)
        failMsg << " fn='" << waitFn << "'";

    return failMsg.str();
}

std::string actionSnapshot(CdpTab &tab, const nlohmann::json &params)
{
    int maxChars = params.value("max_chars", DEFAULT_SNAPSHOT_MAX_CHARS);
    auto format = params.value("format", "text");
    bool interactive = params.value("interactive", false);

    if (format == "accessibility" || interactive)
    {
        // accessibility tree snapshot via JS
        auto js = R"(
            (function() {
                var items = [];
                var refCount = 0;
                function walk(node, depth) {
                    if (depth > )" +
                  std::to_string(MAX_SNAPSHOT_DEPTH) + R"( || items.length > )" + std::to_string(MAX_SNAPSHOT_NODES) + R"() return;
                    var tag = node.tagName ? node.tagName.toLowerCase() : '';
                    if (!tag || tag === 'script' || tag === 'style' || tag === 'noscript' || tag === 'svg') return;

                    var role = node.getAttribute ? (node.getAttribute('role') || '') : '';
                    var ariaLabel = node.getAttribute ? (node.getAttribute('aria-label') || '') : '';
                    var text = '';
                    for (var i = 0; i < node.childNodes.length; i++) {
                        if (node.childNodes[i].nodeType === 3) text += node.childNodes[i].textContent;
                    }
                    text = text.trim().substring(0, 80);

                    var isInteractive = /^(a|button|input|select|textarea)$/.test(tag) ||
                        /^(button|link|textbox|checkbox|radio|combobox|menuitem|tab|switch|slider)$/.test(role) ||
                        node.getAttribute('contenteditable') === 'true' ||
                        (node.getAttribute('tabindex') && node.getAttribute('tabindex') !== '-1');
                    var isStructural = /^(h[1-6]|nav|main|header|footer|section|article|form|table|ul|ol|li|dialog|details|summary)$/.test(tag);
                    var isMedia = tag === 'img' || tag === 'video' || tag === 'audio';

                    if (isInteractive || isStructural || isMedia) {
                        var effectiveRole = role || tag;
                        var name = ariaLabel || text || (node.value || '') || (node.placeholder || '') ||
                                   (node.alt || '') || (node.title || '') || '';
                        name = name.substring(0, 80);

                        var info = {d: depth, r: effectiveRole, n: name};
                        if (isInteractive) {
                            refCount++;
                            info.ref = refCount;
                            if (node.id) info.sel = '#' + CSS.escape(node.id);
                            else if (node.getAttribute('data-testid')) info.sel = '[data-testid="' + node.getAttribute('data-testid') + '"]';
                            else if (ariaLabel) info.sel = '[aria-label="' + ariaLabel.replace(/"/g, '\\"') + '"]';
                            else if (node.name) info.sel = tag + '[name="' + node.name + '"]';
                            else if (node.className && typeof node.className === 'string') {
                                var cls = node.className.trim().split(/\s+/).slice(0, 2).join('.');
                                if (cls) info.sel = tag + '.' + cls;
                                else info.sel = tag;
                            } else info.sel = tag;
                            if (node.type) info.type = node.type;
                        }
                        items.push(info);
                    }

                    for (var c = 0; c < node.children.length; c++) {
                        walk(node.children[c], depth + 1);
                    }
                }
                walk(document.body, 0);
                return items;
            })()
        )";

        auto result = tab.sendCommand(cdp::runtime::Evaluate, {
                                                                  {"expression", js},
                                                                  {"returnByValue", true},
                                                              });

        auto nodes = evalResult(result).value("value", nlohmann::json::array());

        if (nodes.empty())
        {
            return "(empty page - no accessible elements found)";
        }

        std::ostringstream out;
        int interactiveCount = 0;

        for (const auto &node : nodes)
        {
            int depth = node.value("d", 0);
            auto role = node.value("r", "");
            auto name = node.value("n", "");
            bool hasRef = node.contains("ref");

            for (int i = 0; i < depth; ++i)
            {
                out << "  ";
            }

            out << "[" << role;

            if (hasRef)
            {
                out << ":" << node["ref"].get<int>();
                interactiveCount++;
            }

            out << "]";

            if (!name.empty())
            {
                out << " " << name;
            }

            if (hasRef && node.contains("sel"))
            {
                out << "  -> " << node["sel"].get<std::string>();
            }

            if (hasRef && node.contains("type"))
            {
                out << " [" << node["type"].get<std::string>() << "]";
            }

            out << "\n";
        }

        out << "\n(" << nodes.size() << " nodes, " << interactiveCount << " interactive)";

        auto text = out.str();

        if (static_cast<int>(text.size()) > maxChars)
        {
            text = ionclaw::util::StringHelper::utf8SafeTruncate(text, maxChars) + "\n... [truncated]";
        }

        return text;
    }

    // default: plain text snapshot
    auto result = tab.sendCommand(cdp::runtime::Evaluate, {
                                                              {"expression", "document.body.innerText"},
                                                              {"returnByValue", true},
                                                          });

    auto text = evalResult(result).value("value", "");

    if (text.empty())
    {
        return "(empty page)";
    }

    if (static_cast<int>(text.size()) > maxChars)
    {
        text = ionclaw::util::StringHelper::utf8SafeTruncate(text, maxChars) + "\n... [truncated]";
    }

    return text;
}

ToolResult actionScreenshot(CdpTab &tab, const nlohmann::json &params)
{
    bool fullPage = params.value("full_page", false);
    auto imageType = params.value("image_type", "png");
    int quality = params.value("quality", 80);
    auto selector = params.value("selector", "");

    nlohmann::json captureParams = {
        {"format", imageType},
    };

    if (imageType == "jpeg" || imageType == "webp")
    {
        captureParams["quality"] = std::max(0, std::min(100, quality));
    }

    if (fullPage)
    {
        auto layout = tab.sendCommand(cdp::page::GetLayoutMetrics);
        auto contentSize = layout.value("cssContentSize", layout.value("contentSize", nlohmann::json::object()));

        if (contentSize.contains("width") && contentSize.contains("height"))
        {
            captureParams["clip"] = {
                {"x", 0},
                {"y", 0},
                {"width", contentSize["width"]},
                {"height", contentSize["height"]},
                {"scale", 1},
            };
        }
    }
    else if (!selector.empty())
    {
        auto pos = getElementCenter(tab, selector);

        if (!pos.found)
        {
            return "Error: element not found for screenshot: " + selector;
        }

        auto safeSelector = ToolHelper::escapeForJs(selector);
        auto result = tab.sendCommand(cdp::runtime::Evaluate, {
                                                                  {"expression",
                                                                   "(() => { const el = document.querySelector('" + safeSelector + "');"
                                                                                                                                   "if (!el) return null;"
                                                                                                                                   "const r = el.getBoundingClientRect();"
                                                                                                                                   "return {x: r.x, y: r.y, w: r.width, h: r.height}; })()"},
                                                                  {"returnByValue", true},
                                                              });

        auto value = evalResult(result).value("value", nlohmann::json());

        if (!value.is_null() && value.contains("x"))
        {
            captureParams["clip"] = {
                {"x", value["x"]},
                {"y", value["y"]},
                {"width", value["w"]},
                {"height", value["h"]},
                {"scale", 1},
            };
        }
    }

    auto result = tab.sendCommand(cdp::page::CaptureScreenshot, captureParams);
    auto data = result.value("data", "");

    if (data.empty())
    {
        return "Error: no screenshot data returned by browser";
    }

    // decode base64 from CDP
    auto rawBytes = decodeBase64(data);

    if (rawBytes.empty())
    {
        return "Error: failed to decode screenshot data";
    }

    // determine output path: user-specified or temp directory
    auto ext = (imageType == "jpeg") ? ".jpg" : ((imageType == "webp") ? ".webp" : ".png");
    auto outputPath = params.value("output_path", "");
    std::filesystem::path filePath;

    if (!outputPath.empty())
    {
        filePath = std::filesystem::path(outputPath);
        std::error_code ec;
        std::filesystem::create_directories(filePath.parent_path(), ec);
    }
    else
    {
        filePath = browserTempDir() / tempFileName("screenshot", ext);
    }

    if (!saveToFile(filePath, rawBytes.data(), rawBytes.size()))
    {
        return "Error: failed to write screenshot file: " + filePath.string();
    }

    auto fullSizeKB = rawBytes.size() / 1024;

#ifdef IONCLAW_HAS_STB_IMAGE_WRITE
    // load image pixels, resize for LLM-friendly preview, return as small data URI
    constexpr int MAX_PREVIEW_WIDTH = 1024;
    constexpr int PREVIEW_JPEG_QUALITY = 50;

    int w = 0, h = 0, channels = 0;
    unsigned char *pixels = stbi_load_from_memory(rawBytes.data(), static_cast<int>(rawBytes.size()), &w, &h, &channels, 3);

    if (!pixels)
    {
        return "Screenshot saved: " + filePath.string() + " (" + std::to_string(fullSizeKB) + "KB). "
                                                                                              "Warning: could not generate preview (image decode failed).";
    }

    // resize if wider than max preview width
    int previewW = w;
    int previewH = h;
    std::vector<unsigned char> resizedPixels;
    unsigned char *previewData = pixels;

    if (w > MAX_PREVIEW_WIDTH)
    {
        previewW = MAX_PREVIEW_WIDTH;
        previewH = static_cast<int>(static_cast<double>(h) * MAX_PREVIEW_WIDTH / w);

        if (previewH < 1)
            previewH = 1;

        resizedPixels.resize(static_cast<size_t>(previewW * previewH * 3));
        auto *ok = stbir_resize_uint8_linear(pixels, w, h, 0, resizedPixels.data(), previewW, previewH, 0, STBIR_RGB);

        if (!ok)
        {
            stbi_image_free(pixels);
            return "Screenshot saved: " + filePath.string() + " (" + std::to_string(fullSizeKB) + "KB, " +
                   std::to_string(w) + "x" + std::to_string(h) + "). "
                                                                 "Warning: could not generate preview (resize failed).";
        }

        previewData = resizedPixels.data();
    }

    // encode preview as JPEG to memory
    std::vector<unsigned char> jpegBuf;
    jpegBuf.reserve(static_cast<size_t>(previewW * previewH));
    stbi_write_jpg_to_func(stbWriteToVector, &jpegBuf, previewW, previewH, 3, previewData, PREVIEW_JPEG_QUALITY);

    stbi_image_free(pixels);

    if (jpegBuf.empty())
    {
        return "Screenshot saved: " + filePath.string() + " (" + std::to_string(fullSizeKB) + "KB, " +
               std::to_string(w) + "x" + std::to_string(h) + "). "
                                                             "Warning: could not generate preview (JPEG encode failed).";
    }

    auto previewB64 = encodeBase64(jpegBuf.data(), jpegBuf.size());
    auto previewKB = jpegBuf.size() / 1024;

    auto description = "Screenshot captured (" + std::to_string(w) + "x" + std::to_string(h) + ", " +
                       std::to_string(fullSizeKB) + "KB). " +
                       "Preview: " + std::to_string(previewW) + "x" + std::to_string(previewH) + " (" +
                       std::to_string(previewKB) + "KB). " +
                       "Full resolution: " + filePath.string();

    ToolResult toolResult;
    toolResult.text = description;
    toolResult.media = nlohmann::json::array({
        {{"type", "image"}, {"media_type", "image/jpeg"}, {"data", previewB64}},
    });
    return toolResult;
#else
    return "Screenshot saved: " + filePath.string() + " (" + std::to_string(fullSizeKB) + "KB, " + imageType + "). "
                                                                                                               "Note: preview not available (stb not compiled). Use the file path to view the screenshot.";
#endif
}

std::string actionInspect(CdpTab &tab)
{
    auto js = R"(
        (function() {
            var items = [];
            var seen = new Set();
            var els = document.querySelectorAll(
                'a, button, input, textarea, select, [role="button"], [role="link"], '
                + '[role="textbox"], [contenteditable="true"], [tabindex]:not([tabindex="-1"])'
            );
            for (var i = 0; i < els.length && items.length < )" +
              std::to_string(MAX_INTERACTIVE_ELEMENTS) + R"(; i++) {
                var el = els[i];
                var rect = el.getBoundingClientRect();
                if (rect.width === 0 || rect.height === 0) continue;
                if (rect.bottom < 0 || rect.top > window.innerHeight) continue;

                var selector = '';
                if (el.id) {
                    selector = '#' + CSS.escape(el.id);
                } else if (el.getAttribute('data-testid')) {
                    selector = '[data-testid="' + el.getAttribute('data-testid') + '"]';
                } else if (el.getAttribute('aria-label')) {
                    selector = '[aria-label="' + el.getAttribute('aria-label').replace(/"/g, '\\"') + '"]';
                } else if (el.name) {
                    selector = el.tagName.toLowerCase() + '[name="' + el.name + '"]';
                } else if (el.className && typeof el.className === 'string') {
                    var cls = el.className.trim().split(/\s+/).slice(0, 2).join('.');
                    if (cls) selector = el.tagName.toLowerCase() + '.' + cls;
                    else selector = el.tagName.toLowerCase();
                } else {
                    selector = el.tagName.toLowerCase();
                }

                if (seen.has(selector)) continue;
                seen.add(selector);

                var label = (
                    el.textContent || el.getAttribute('aria-label')
                    || el.getAttribute('placeholder') || el.value || ''
                ).trim().substring(0, 60);

                var tag = el.tagName.toLowerCase();
                var role = el.getAttribute('role') || '';
                var type = el.type || '';
                var desc = tag;
                if (role) desc += '[' + role + ']';
                if (type) desc += '[' + type + ']';

                items.push({s: selector, t: desc, l: label, h: el.href || ''});
            }
            return items;
        })()
    )";

    auto result = tab.sendCommand(cdp::runtime::Evaluate, {
                                                              {"expression", js},
                                                              {"returnByValue", true},
                                                          });

    auto elements = evalResult(result).value("value", nlohmann::json::array());

    if (elements.empty())
    {
        return "No interactive elements found on page.";
    }

    std::ostringstream out;
    out << "Interactive elements on page (" << elements.size() << "):\n\n";

    for (const auto &el : elements)
    {
        auto label = el.value("l", "");
        out << "  " << el.value("t", "");

        if (!label.empty())
        {
            out << " \"" << label << "\"";
        }

        auto href = el.value("h", "");

        if (!href.empty())
        {
            out << " href=" << href;
        }

        out << "  ->  selector: " << el.value("s", "") << "\n";
    }

    return capResult(out.str());
}

std::string actionEvaluate(CdpTab &tab, const std::string &script)
{
    if (script.empty())
    {
        return "Error: 'script' is required for evaluate action";
    }

    auto result = tab.sendCommand(cdp::runtime::Evaluate, {
                                                              {"expression", script},
                                                              {"returnByValue", true},
                                                              {"awaitPromise", true},
                                                          });

    // check for JavaScript exceptions
    if (result.contains("exceptionDetails"))
    {
        auto &exc = result["exceptionDetails"];
        auto text = exc.value("text", "");

        if (exc.contains("exception") && exc["exception"].contains("description"))
        {
            return "Error: JavaScript exception: " + exc["exception"]["description"].get<std::string>();
        }

        if (!text.empty())
        {
            return "Error: JavaScript exception: " + text;
        }

        return "Error: JavaScript exception (unknown details)";
    }

    auto value = evalResult(result);

    if (value.contains("value"))
    {
        if (value["value"].is_string())
        {
            return capResult(value["value"].get<std::string>());
        }
        else if (value["value"].is_null())
        {
            return "(null)";
        }
        else
        {
            return capResult(value["value"].dump(2));
        }
    }

    if (value.contains("description"))
    {
        return capResult(value["description"].get<std::string>());
    }

    return capResult(value.dump(2));
}

std::string actionClick(CdpTab &tab, const nlohmann::json &params)
{
    auto selector = params.value("selector", "");

    if (selector.empty())
    {
        return "Error: 'selector' is required for click action";
    }

    auto pos = getElementCenter(tab, selector);

    if (!pos.found)
    {
        return "Error: element not found: " + selector;
    }

    auto button = params.value("button", "left");
    bool doubleClick = params.value("double_click", false);

    performClick(tab, pos.x, pos.y, button, doubleClick);
    return "Clicked: " + selector + (doubleClick ? " (double-click)" : "");
}

std::string actionType(CdpTab &tab, const nlohmann::json &params)
{
    auto text = params.value("text", "");

    if (text.empty())
    {
        return "Error: 'text' is required for type action";
    }

    auto selector = params.value("selector", "");
    bool submit = params.value("submit", false);
    bool slowly = params.value("slowly", false);

    if (!selector.empty())
    {
        auto pos = getElementCenter(tab, selector);

        if (!pos.found)
        {
            return "Error: element not found: " + selector;
        }

        performClick(tab, pos.x, pos.y);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    typeCharacters(tab, text, slowly);

    if (submit)
    {
        pressKey(tab, "Enter");
    }

    return "Typed: " + text + (submit ? " (submitted)" : "");
}

std::string actionPress(CdpTab &tab, const nlohmann::json &params)
{
    auto key = params.value("key", "");

    if (key.empty())
    {
        return "Error: 'key' is required for press action";
    }

    auto err = pressKey(tab, key);

    if (!err.empty())
    {
        return err;
    }

    return "Pressed: " + key;
}

std::string actionHover(CdpTab &tab, const nlohmann::json &params)
{
    auto selector = params.value("selector", "");

    if (selector.empty())
    {
        return "Error: 'selector' is required for hover action";
    }

    auto pos = getElementCenter(tab, selector);

    if (!pos.found)
    {
        return "Error: element not found: " + selector;
    }

    tab.sendCommand(cdp::input::DispatchMouseEvent, {
                                                        {"type", "mouseMoved"},
                                                        {"x", pos.x},
                                                        {"y", pos.y},
                                                    });

    return "Hovered: " + selector;
}

std::string actionSelect(CdpTab &tab, const nlohmann::json &params)
{
    auto selector = params.value("selector", "");

    if (selector.empty())
    {
        return "Error: 'selector' is required for select action";
    }

    auto values = params.value("values", nlohmann::json::array());

    if (values.empty())
    {
        return "Error: 'values' is required for select action";
    }

    auto safeSelector = ToolHelper::escapeForJs(selector);
    auto valuesJson = values.dump();

    auto js = "(() => {"
              "const el = document.querySelector('" +
              safeSelector + "');"
                             "if (!el || el.tagName.toLowerCase() !== 'select') return null;"
                             "const vals = " +
              valuesJson + ";"
                           "for (const opt of el.options) {"
                           "  opt.selected = vals.includes(opt.value) || vals.includes(opt.textContent.trim());"
                           "}"
                           "el.dispatchEvent(new Event('change', {bubbles: true}));"
                           "return Array.from(el.selectedOptions).map(o => o.value);"
                           "})()";

    auto result = tab.sendCommand(cdp::runtime::Evaluate, {
                                                              {"expression", js},
                                                              {"returnByValue", true},
                                                          });

    auto selected = evalResult(result).value("value", nlohmann::json());

    if (selected.is_null())
    {
        return "Error: select element not found: " + selector;
    }

    return capResult("Selected: " + selected.dump());
}

std::string actionFill(CdpTab &tab, const nlohmann::json &params)
{
    auto fields = params.value("fields", nlohmann::json::array());

    if (fields.empty())
    {
        return "Error: 'fields' array is required for fill action";
    }

    int filled = 0;
    int skipped = 0;

    for (const auto &field : fields)
    {
        auto selector = field.value("selector", "");

        if (selector.empty())
        {
            skipped++;
            spdlog::warn("browser: fill skipped field with empty selector");
            continue;
        }

        auto safeSelector = ToolHelper::escapeForJs(selector);
        auto type = field.value("type", "");

        // auto-detect type if not specified
        if (type.empty())
        {
            auto detectResult = tab.sendCommand(cdp::runtime::Evaluate, {
                                                                            {"expression", "(() => { const el = document.querySelector('" + safeSelector + "'); return el ? el.type || el.tagName.toLowerCase() : ''; })()"},
                                                                            {"returnByValue", true},
                                                                        });
            type = evalResult(detectResult).value("value", "text");
        }

        if (type == "checkbox" || type == "radio")
        {
            bool wantChecked = false;

            if (field.contains("value"))
            {
                if (field["value"].is_boolean())
                {
                    wantChecked = field["value"].get<bool>();
                }
                else if (field["value"].is_string())
                {
                    wantChecked = field["value"].get<std::string>() == "true";
                }
                else
                {
                    // numeric or other: truthy if non-zero
                    wantChecked = !field["value"].empty() && field["value"].dump() != "0" && field["value"].dump() != "null";
                }
            }

            auto checkResult = tab.sendCommand(cdp::runtime::Evaluate, {
                                                                           {"expression", "document.querySelector('" + safeSelector + "').checked"},
                                                                           {"returnByValue", true},
                                                                       });

            bool isChecked = evalResult(checkResult).value("value", false);

            if (isChecked != wantChecked)
            {
                auto pos = getElementCenter(tab, selector);

                if (pos.found)
                {
                    performClick(tab, pos.x, pos.y);
                }
            }
        }
        else
        {
            // text-like input: clear + type value
            auto value = field.value("value", "");

            tab.sendCommand(cdp::runtime::Evaluate, {
                                                        {"expression", "(() => { const el = document.querySelector('" + safeSelector + "');"
                                                                                                                                       "if (el) { el.focus(); el.value = ''; el.dispatchEvent(new Event('input', {bubbles:true})); } })()"},
                                                        {"returnByValue", true},
                                                    });

            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            if (!value.empty())
            {
                typeCharacters(tab, value);
            }

            tab.sendCommand(cdp::runtime::Evaluate, {
                                                        {"expression", "(() => { const el = document.querySelector('" + safeSelector + "');"
                                                                                                                                       "if (el) el.dispatchEvent(new Event('change', {bubbles:true})); })()"},
                                                        {"returnByValue", true},
                                                    });
        }

        filled++;
    }

    return "Filled " + std::to_string(filled) + " field(s)." +
           (skipped > 0 ? " Warning: " + std::to_string(skipped) + " field(s) skipped (missing selector)." : "");
}

std::string actionDrag(CdpTab &tab, const nlohmann::json &params)
{
    auto selector = params.value("selector", "");
    auto endSelector = params.value("end_selector", "");

    if (selector.empty() || endSelector.empty())
    {
        return "Error: 'selector' and 'end_selector' are required for drag action";
    }

    auto startPos = getElementCenter(tab, selector);

    if (!startPos.found)
    {
        return "Error: drag source element not found: " + selector;
    }

    auto endPos = getElementCenter(tab, endSelector);

    if (!endPos.found)
    {
        return "Error: drag target element not found: " + endSelector;
    }

    tab.sendCommand(cdp::input::DispatchMouseEvent, {
                                                        {"type", "mouseMoved"},
                                                        {"x", startPos.x},
                                                        {"y", startPos.y},
                                                    });

    tab.sendCommand(cdp::input::DispatchMouseEvent, {
                                                        {"type", "mousePressed"},
                                                        {"x", startPos.x},
                                                        {"y", startPos.y},
                                                        {"button", "left"},
                                                        {"clickCount", 1},
                                                        {"buttons", 1},
                                                    });

    // intermediate steps for smooth drag
    int steps = 10;

    for (int i = 1; i <= steps; ++i)
    {
        double t = static_cast<double>(i) / steps;
        double mx = startPos.x + (endPos.x - startPos.x) * t;
        double my = startPos.y + (endPos.y - startPos.y) * t;

        tab.sendCommand(cdp::input::DispatchMouseEvent, {
                                                            {"type", "mouseMoved"},
                                                            {"x", mx},
                                                            {"y", my},
                                                            {"buttons", 1},
                                                        });
    }

    tab.sendCommand(cdp::input::DispatchMouseEvent, {
                                                        {"type", "mouseReleased"},
                                                        {"x", endPos.x},
                                                        {"y", endPos.y},
                                                        {"button", "left"},
                                                    });

    return "Dragged from " + selector + " to " + endSelector;
}

std::string actionScrollIntoView(CdpTab &tab, const nlohmann::json &params)
{
    auto selector = params.value("selector", "");

    if (selector.empty())
    {
        return "Error: 'selector' is required for scroll_into_view action";
    }

    auto safeSelector = ToolHelper::escapeForJs(selector);

    auto result = tab.sendCommand(cdp::runtime::Evaluate, {
                                                              {"expression",
                                                               "(() => { const el = document.querySelector('" + safeSelector + "');"
                                                                                                                               "if (!el) return false;"
                                                                                                                               "el.scrollIntoView({behavior:'smooth', block:'center', inline:'center'});"
                                                                                                                               "return true; })()"},
                                                              {"returnByValue", true},
                                                          });

    if (!evalResult(result).value("value", false))
    {
        return "Error: element not found: " + selector;
    }

    return "Scrolled into view: " + selector;
}

std::string actionResize(CdpTab &tab, const nlohmann::json &params)
{
    int width = params.value("width", 1280);
    int height = params.value("height", 720);

    if (width <= 0 || height <= 0)
    {
        return "Error: viewport dimensions must be positive (got " +
               std::to_string(width) + "x" + std::to_string(height) + ").";
    }

    if (width > 7680 || height > 4320)
    {
        return "Error: viewport dimensions too large (max 7680x4320, got " +
               std::to_string(width) + "x" + std::to_string(height) + ").";
    }

    tab.sendCommand(cdp::emulation::SetDeviceMetricsOverride, {
                                                                  {"width", width},
                                                                  {"height", height},
                                                                  {"deviceScaleFactor", 1},
                                                                  {"mobile", false},
                                                              });

    return "Viewport resized to " + std::to_string(width) + "x" + std::to_string(height);
}

std::string actionConsole(CdpTab &tab, const nlohmann::json &params)
{
    tab.drainEvents();
    auto level = params.value("level", "");
    bool clear = params.value("clear", false);
    auto messages = tab.getConsole(clear);

    if (messages.empty())
    {
        return "No console messages.";
    }

    std::ostringstream out;
    int count = 0;

    for (const auto &msg : messages)
    {
        if (!level.empty() && msg.level != level)
        {
            continue;
        }

        out << "[" << msg.level << "] " << msg.text << "\n";
        count++;
    }

    if (count == 0)
    {
        return "No console messages" + (level.empty() ? "." : " matching level '" + level + "'.");
    }

    out << "\n(" << count << " messages)";
    return capResult(out.str());
}

std::string actionErrors(CdpTab &tab, const nlohmann::json &params)
{
    tab.drainEvents();
    bool clear = params.value("clear", false);
    auto errors = tab.getErrors(clear);

    if (errors.empty())
    {
        return "No page errors.";
    }

    std::ostringstream out;

    for (const auto &err : errors)
    {
        out << err.message;

        if (!err.url.empty())
        {
            out << " (" << err.url << ":" << err.line << ":" << err.column << ")";
        }

        out << "\n";
    }

    out << "\n(" << errors.size() << " errors)";
    return capResult(out.str());
}

std::string actionRequests(CdpTab &tab, const nlohmann::json &params)
{
    tab.drainEvents();
    auto filter = params.value("filter", "");
    bool clear = params.value("clear", false);
    auto requests = tab.getRequests(clear);

    if (requests.empty())
    {
        return "No network requests.";
    }

    std::ostringstream out;
    int count = 0;

    for (const auto &req : requests)
    {
        if (!filter.empty() && req.url.find(filter) == std::string::npos)
        {
            continue;
        }

        out << req.method << " " << req.statusCode << " " << req.url;

        if (!req.mimeType.empty())
        {
            out << " [" << req.mimeType << "]";
        }

        out << "\n";
        count++;
    }

    if (count == 0)
    {
        return "No requests" + (filter.empty() ? "." : " matching '" + filter + "'.");
    }

    out << "\n(" << count << " requests)";
    return capResult(out.str());
}

std::string actionResponseBody(CdpTab &tab, const nlohmann::json &params)
{
    auto filter = params.value("filter", "");
    int maxChars = params.value("max_chars", DEFAULT_SNAPSHOT_MAX_CHARS);

    if (filter.empty())
    {
        return "Error: 'filter' is required to identify the request";
    }

    tab.drainEvents();
    auto requests = tab.getRequests(false);

    // find matching request (most recent)
    std::string matchedRequestId;

    for (auto it = requests.rbegin(); it != requests.rend(); ++it)
    {
        if (it->url.find(filter) != std::string::npos && it->finished)
        {
            matchedRequestId = it->requestId;
            break;
        }
    }

    if (matchedRequestId.empty())
    {
        return "Error: no completed request matching '" + filter + "'";
    }

    auto result = tab.sendCommand(cdp::network::GetResponseBody, {
                                                                     {"requestId", matchedRequestId},
                                                                 });

    auto body = result.value("body", "");
    bool base64Encoded = result.value("base64Encoded", false);

    if (base64Encoded)
    {
        return "(binary content, " + std::to_string(body.size()) + " bytes base64)";
    }

    if (static_cast<int>(body.size()) > maxChars)
    {
        body = ionclaw::util::StringHelper::utf8SafeTruncate(body, maxChars) + "\n... [truncated]";
    }

    return body;
}

std::string actionPdf(CdpTab &tab, const nlohmann::json &params)
{
    auto result = tab.sendCommand(cdp::page::PrintToPDF, {
                                                             {"landscape", false},
                                                             {"displayHeaderFooter", false},
                                                             {"printBackground", true},
                                                             {"preferCSSPageSize", true},
                                                         });

    auto data = result.value("data", "");

    if (data.empty())
    {
        return "Error: PDF generation failed";
    }

    auto rawBytes = decodeBase64(data);

    if (rawBytes.empty())
    {
        return "Error: failed to decode PDF data";
    }

    auto outputPath = params.value("output_path", "");
    std::filesystem::path filePath;

    if (!outputPath.empty())
    {
        filePath = std::filesystem::path(outputPath);
        std::error_code ec;
        std::filesystem::create_directories(filePath.parent_path(), ec);
    }
    else
    {
        filePath = browserTempDir() / tempFileName("page", ".pdf");
    }

    if (!saveToFile(filePath, rawBytes.data(), rawBytes.size()))
    {
        return "Error: failed to write PDF file: " + filePath.string();
    }

    auto sizeKB = rawBytes.size() / 1024;
    return "PDF saved: " + filePath.string() + " (" + std::to_string(sizeKB) + "KB)";
}

std::string actionGetCookies(CdpTab &tab, const nlohmann::json &params)
{
    auto urlsParam = nlohmann::json::object();
    auto url = params.value("url", "");

    if (!url.empty())
    {
        urlsParam["urls"] = nlohmann::json::array({url});
    }

    auto result = tab.sendCommand(cdp::network::GetCookies, urlsParam);
    auto cookies = result.value("cookies", nlohmann::json::array());

    if (cookies.empty())
    {
        return "No cookies.";
    }

    std::ostringstream out;

    for (const auto &cookie : cookies)
    {
        out << cookie.value("name", "") << "=" << cookie.value("value", "")
            << " (domain=" << cookie.value("domain", "")
            << ", path=" << cookie.value("path", "")
            << ", httpOnly=" << (cookie.value("httpOnly", false) ? "true" : "false")
            << ", secure=" << (cookie.value("secure", false) ? "true" : "false")
            << ")\n";
    }

    out << "\n(" << cookies.size() << " cookies)";
    return capResult(out.str());
}

std::string actionSetCookie(CdpTab &tab, const nlohmann::json &params)
{
    auto name = params.value("name", "");
    auto value = params.value("value", "");

    if (name.empty())
    {
        return "Error: 'name' is required for set_cookie action";
    }

    nlohmann::json cookieParams = {
        {"name", name},
        {"value", value},
    };

    auto domain = params.value("domain", "");
    auto path = params.value("path", "/");
    auto url = params.value("url", "");

    if (!url.empty())
    {
        cookieParams["url"] = url;
    }

    if (!domain.empty())
    {
        cookieParams["domain"] = domain;
    }

    cookieParams["path"] = path;

    tab.sendCommand(cdp::network::SetCookie, cookieParams);
    return "Cookie set: " + name + "=" + value;
}

std::string actionClearCookies(CdpTab &tab)
{
    tab.sendCommand(cdp::network::ClearBrowserCookies);
    return "Cookies cleared.";
}

std::string actionGetStorage(CdpTab &tab, const nlohmann::json &params)
{
    auto kind = params.value("kind", "local");
    auto storageObj = (kind == "session") ? "sessionStorage" : "localStorage";

    auto result = tab.sendCommand(cdp::runtime::Evaluate, {
                                                              {"expression", "JSON.stringify(" + std::string(storageObj) + ")"},
                                                              {"returnByValue", true},
                                                          });

    auto value = evalResult(result).value("value", "{}");
    auto parsed = nlohmann::json::parse(value, nullptr, false);

    if (parsed.is_discarded() || parsed.empty())
    {
        return "No " + kind + " storage entries.";
    }

    return capResult(parsed.dump(2));
}

std::string actionSetStorage(CdpTab &tab, const nlohmann::json &params)
{
    auto kind = params.value("kind", "local");
    auto name = params.value("name", "");
    auto value = params.value("value", "");

    if (name.empty())
    {
        return "Error: 'name' is required for set_storage action";
    }

    auto storageObj = (kind == "session") ? "sessionStorage" : "localStorage";
    auto safeKey = ToolHelper::escapeForJs(name);
    auto safeValue = ToolHelper::escapeForJs(value);

    tab.sendCommand(cdp::runtime::Evaluate, {
                                                {"expression", std::string(storageObj) + ".setItem('" + safeKey + "', '" + safeValue + "')"},
                                                {"returnByValue", true},
                                            });

    return kind + " storage set: " + name + "=" + value;
}

std::string actionClearStorage(CdpTab &tab, const nlohmann::json &params)
{
    auto kind = params.value("kind", "local");
    auto storageObj = (kind == "session") ? "sessionStorage" : "localStorage";

    tab.sendCommand(cdp::runtime::Evaluate, {
                                                {"expression", std::string(storageObj) + ".clear()"},
                                                {"returnByValue", true},
                                            });

    return kind + " storage cleared.";
}

std::string actionSetOffline(CdpTab &tab, const nlohmann::json &params)
{
    bool enabled = params.value("enabled", true);

    tab.sendCommand(cdp::network::EmulateNetworkConditions, {
                                                                {"offline", enabled},
                                                                {"latency", 0},
                                                                {"downloadThroughput", -1},
                                                                {"uploadThroughput", -1},
                                                            });

    return "Offline mode " + std::string(enabled ? "enabled" : "disabled") + ".";
}

std::string actionSetHeaders(CdpTab &tab, const nlohmann::json &params)
{
    auto headers = params.value("headers", nlohmann::json::object());

    if (headers.empty())
    {
        return "Error: 'headers' object is required for set_headers action";
    }

    tab.sendCommand(cdp::network::SetExtraHTTPHeaders, {
                                                           {"headers", headers},
                                                       });

    return "Custom headers set (" + std::to_string(headers.size()) + " headers).";
}

std::string actionSetCredentials(CdpTab &tab, const nlohmann::json &params)
{
    auto username = params.value("username", "");
    auto password = params.value("password", "");
    bool clear = params.value("clear", false);

    if (clear)
    {
        tab.clearAuthCredentials();
        tab.sendCommand(cdp::fetch::Disable);
        return "HTTP credentials cleared.";
    }

    if (username.empty())
    {
        return "Error: 'username' is required for set_credentials action";
    }

    tab.setAuthCredentials(username, password);
    tab.sendCommand(cdp::fetch::Enable, {
                                            {"handleAuthRequests", true},
                                        });

    return "HTTP credentials set for " + username + ".";
}

std::string actionSetGeolocation(CdpTab &tab, const nlohmann::json &params)
{
    bool clear = params.value("clear", false);

    if (clear)
    {
        tab.sendCommand(cdp::emulation::ClearGeolocationOverride);
        return "Geolocation override cleared.";
    }

    double latitude = params.value("latitude", 0.0);
    double longitude = params.value("longitude", 0.0);

    if (latitude < -90.0 || latitude > 90.0 || longitude < -180.0 || longitude > 180.0)
    {
        return "Error: invalid coordinates. Latitude must be -90 to 90, longitude -180 to 180 (got " +
               std::to_string(latitude) + ", " + std::to_string(longitude) + ").";
    }

    tab.sendCommand(cdp::emulation::SetGeolocationOverride, {
                                                                {"latitude", latitude},
                                                                {"longitude", longitude},
                                                                {"accuracy", 1.0},
                                                            });

    return "Geolocation set to " + std::to_string(latitude) + ", " + std::to_string(longitude) + ".";
}

std::string actionSetMedia(CdpTab &tab, const nlohmann::json &params)
{
    auto media = params.value("media", "");

    if (media.empty() || media == "none")
    {
        tab.sendCommand(cdp::emulation::SetEmulatedMedia, {
                                                              {"features", nlohmann::json::array()},
                                                          });
        return "Media emulation cleared.";
    }

    tab.sendCommand(cdp::emulation::SetEmulatedMedia, {
                                                          {"features", nlohmann::json::array({
                                                                           {{"name", "prefers-color-scheme"}, {"value", media}},
                                                                       })},
                                                      });

    return "Media emulation set to: " + media;
}

std::string actionSetTimezone(CdpTab &tab, const nlohmann::json &params)
{
    auto timezone = params.value("timezone", "");

    if (timezone.empty())
    {
        // CDP requires a valid timezone ID; use UTC to effectively "clear" the override,
        // then reload to apply the system timezone via a fresh page load
        tab.sendCommand(cdp::emulation::SetTimezoneOverride, {{"timezoneId", "Etc/UTC"}});
        return "Timezone override set to UTC. To fully restore the system timezone, reload the page after clearing.";
    }

    tab.sendCommand(cdp::emulation::SetTimezoneOverride, {
                                                             {"timezoneId", timezone},
                                                         });

    return "Timezone set to: " + timezone;
}

std::string actionSetLocale(CdpTab &tab, const nlohmann::json &params)
{
    auto locale = params.value("locale", "");

    if (locale.empty())
    {
        return "Error: 'locale' is required for set_locale action";
    }

    tab.sendCommand(cdp::emulation::SetLocaleOverride, {
                                                           {"locale", locale},
                                                       });

    return "Locale set to: " + locale;
}

std::string actionSetDevice(CdpTab &tab, const nlohmann::json &params)
{
    auto device = params.value("device", "");
    bool clear = params.value("clear", false);

    if (clear)
    {
        tab.sendCommand(cdp::emulation::ClearDeviceMetricsOverride);
        return "Device emulation cleared.";
    }

    int width = params.value("width", 0);
    int height = params.value("height", 0);

    // check for device preset
    if (!device.empty())
    {
        std::string lower = device;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c)
                       { return c < 0x80 ? std::tolower(c) : c; });

        auto it = DEVICE_PRESETS.find(lower);

        if (it != DEVICE_PRESETS.end())
        {
            auto &preset = it->second;

            tab.sendCommand(cdp::emulation::SetDeviceMetricsOverride, {
                                                                          {"width", preset.width},
                                                                          {"height", preset.height},
                                                                          {"deviceScaleFactor", preset.deviceScaleFactor},
                                                                          {"mobile", preset.isMobile},
                                                                      });

            if (!preset.userAgent.empty())
            {
                tab.sendCommand(cdp::emulation::SetUserAgentOverride, {
                                                                          {"userAgent", preset.userAgent},
                                                                      });
            }

            return "Device emulation set to: " + device + " (" + std::to_string(preset.width) + "x" + std::to_string(preset.height) + ")";
        }

        return "Error: unknown device preset '" + device + "'. Available: "
                                                           "iPhone 14, iPhone 14 Pro Max, iPhone SE, iPad, iPad Pro, "
                                                           "Pixel 7, Samsung Galaxy S23, Desktop 1080p, Desktop 1440p";
    }

    if (width > 0 && height > 0)
    {
        if (width > 7680 || height > 4320)
        {
            return "Error: viewport dimensions too large (max 7680x4320, got " +
                   std::to_string(width) + "x" + std::to_string(height) + ").";
        }

        tab.sendCommand(cdp::emulation::SetDeviceMetricsOverride, {
                                                                      {"width", width},
                                                                      {"height", height},
                                                                      {"deviceScaleFactor", 1},
                                                                      {"mobile", false},
                                                                  });

        return "Viewport set to " + std::to_string(width) + "x" + std::to_string(height);
    }

    return "Error: specify 'device' preset name or 'width'+'height'";
}

std::string actionDialog(CdpTab &tab, const nlohmann::json &params)
{
    bool accept = params.value("accept", true);
    auto promptText = params.value("prompt_text", "");

    nlohmann::json dialogParams = {
        {"accept", accept},
    };

    if (!promptText.empty())
    {
        dialogParams["promptText"] = promptText;
    }

    tab.sendCommand(cdp::page::HandleJavaScriptDialog, dialogParams);
    tab.clearPendingDialog();
    return "Dialog " + std::string(accept ? "accepted" : "dismissed") + ".";
}

std::string actionUpload(CdpTab &tab, const nlohmann::json &params)
{
    auto selector = params.value("selector", "");
    auto path = params.value("path", "");

    if (selector.empty())
    {
        return "Error: 'selector' is required for upload action";
    }

    if (path.empty())
    {
        return "Error: 'path' is required for upload action";
    }

    if (!std::filesystem::exists(path))
    {
        return "Error: file not found: " + path;
    }

    if (!std::filesystem::is_regular_file(path))
    {
        return "Error: not a regular file: " + path;
    }

    auto safeSelector = ToolHelper::escapeForJs(selector);

    // resolve the input element
    auto resolveResult = tab.sendCommand(cdp::runtime::Evaluate, {
                                                                     {"expression", "document.querySelector('" + safeSelector + "')"},
                                                                     {"returnByValue", false},
                                                                 });

    auto objectId = evalResult(resolveResult).value("objectId", "");

    if (objectId.empty())
    {
        return "Error: file input element not found: " + selector;
    }

    // get the backend node ID
    auto nodeResult = tab.sendCommand(cdp::dom::DescribeNode, {
                                                                  {"objectId", objectId},
                                                              });

    auto backendNodeId = nodeResult.value("node", nlohmann::json::object()).value("backendNodeId", 0);

    if (backendNodeId == 0)
    {
        return "Error: could not resolve DOM node for: " + selector;
    }

    // set the files
    tab.sendCommand(cdp::dom::SetFileInputFiles, {
                                                     {"files", nlohmann::json::array({path})},
                                                     {"backendNodeId", backendNodeId},
                                                 });

    return "File uploaded: " + path;
}

} // anonymous namespace

// ── BrowserTool public interface ───────────────────────────────────────────

ToolResult BrowserTool::execute(const nlohmann::json &params, const ToolContext & /*context*/)
{
    auto action = params.value("action", "");

    if (action.empty())
    {
        return "Error: 'action' parameter is required. Available actions: navigate, snapshot, screenshot, "
               "click, type, press, hover, select, fill, drag, scroll, scroll_into_view, resize, evaluate, "
               "console, errors, requests, response_body, pdf, cookies, set_cookie, clear_cookies, "
               "get_storage, set_storage, clear_storage, set_offline, set_headers, set_credentials, "
               "set_geolocation, set_media, set_timezone, set_locale, set_device, dialog, upload, wait, "
               "inspect, tabs, open, focus, close, back, forward, reload, status, start, stop.";
    }

    bool headless = false;

    if (params.contains("headless") && params["headless"].is_boolean())
    {
        headless = params["headless"].get<bool>();
    }

    try
    {
        // lifecycle actions (no tab needed)
        if (action == "status")
        {
            return actionStatus();
        }

        if (action == "start")
        {
            return actionStart(headless);
        }

        if (action == "stop")
        {
            return actionStop();
        }

        // tab management actions (browser must be running)
        if (action == "tabs")
        {
            auto err = ensureBrowser(headless);

            if (!err.empty())
            {
                return err;
            }

            return actionTabs();
        }

        if (action == "open")
        {
            auto url = params.value("url", "about:blank");
            return actionOpen(headless, url);
        }

        if (action == "focus")
        {
            return actionFocus(params.value("target_id", ""));
        }

        // wait can work without a tab for simple time waits
        if (action == "wait")
        {
            auto result = ensureTab(headless);
            return actionWait(result.tab, params);
        }

        // close tab
        if (action == "close")
        {
            return actionClose(params.value("target_id", ""));
        }

        // all remaining actions need a connected tab
        auto tabResult = ensureTab(headless);

        if (!tabResult.tab)
        {
            return tabResult.error;
        }

        auto *tab = tabResult.tab;

        // navigation
        if (action == "navigate")
        {
            return actionNavigate(*tab, params.value("url", ""));
        }

        if (action == "back")
        {
            return actionBack(*tab);
        }

        if (action == "forward")
        {
            return actionForward(*tab);
        }

        if (action == "reload")
        {
            return actionReload(*tab);
        }

        if (action == "scroll")
        {
            return actionScroll(*tab, params);
        }

        // observation
        if (action == "snapshot")
        {
            return actionSnapshot(*tab, params);
        }

        if (action == "screenshot")
        {
            return actionScreenshot(*tab, params);
        }

        if (action == "inspect")
        {
            return actionInspect(*tab);
        }

        if (action == "console")
        {
            return actionConsole(*tab, params);
        }

        if (action == "errors")
        {
            return actionErrors(*tab, params);
        }

        if (action == "requests")
        {
            return actionRequests(*tab, params);
        }

        if (action == "response_body")
        {
            return actionResponseBody(*tab, params);
        }

        if (action == "pdf")
        {
            return actionPdf(*tab, params);
        }

        // interaction
        if (action == "click")
        {
            return actionClick(*tab, params);
        }

        if (action == "type")
        {
            return actionType(*tab, params);
        }

        if (action == "press")
        {
            return actionPress(*tab, params);
        }

        if (action == "hover")
        {
            return actionHover(*tab, params);
        }

        if (action == "select")
        {
            return actionSelect(*tab, params);
        }

        if (action == "fill")
        {
            return actionFill(*tab, params);
        }

        if (action == "drag")
        {
            return actionDrag(*tab, params);
        }

        if (action == "scroll_into_view")
        {
            return actionScrollIntoView(*tab, params);
        }

        if (action == "resize")
        {
            return actionResize(*tab, params);
        }

        // evaluation
        if (action == "evaluate")
        {
            return actionEvaluate(*tab, params.value("script", ""));
        }

        // cookies
        if (action == "cookies")
        {
            return actionGetCookies(*tab, params);
        }

        if (action == "set_cookie")
        {
            return actionSetCookie(*tab, params);
        }

        if (action == "clear_cookies")
        {
            return actionClearCookies(*tab);
        }

        // storage
        if (action == "get_storage")
        {
            return actionGetStorage(*tab, params);
        }

        if (action == "set_storage")
        {
            return actionSetStorage(*tab, params);
        }

        if (action == "clear_storage")
        {
            return actionClearStorage(*tab, params);
        }

        // emulation
        if (action == "set_offline")
        {
            return actionSetOffline(*tab, params);
        }

        if (action == "set_headers")
        {
            return actionSetHeaders(*tab, params);
        }

        if (action == "set_credentials")
        {
            return actionSetCredentials(*tab, params);
        }

        if (action == "set_geolocation")
        {
            return actionSetGeolocation(*tab, params);
        }

        if (action == "set_media")
        {
            return actionSetMedia(*tab, params);
        }

        if (action == "set_timezone")
        {
            return actionSetTimezone(*tab, params);
        }

        if (action == "set_locale")
        {
            return actionSetLocale(*tab, params);
        }

        if (action == "set_device")
        {
            return actionSetDevice(*tab, params);
        }

        // file/dialog handling
        if (action == "dialog")
        {
            return actionDialog(*tab, params);
        }

        if (action == "upload")
        {
            return actionUpload(*tab, params);
        }

        return "Error: unknown action: " + action;
    }
    catch (const std::exception &e)
    {
        spdlog::error("browser: action '{}' failed: {}", action, e.what());

        // on CDP error, only disconnect the failed tab (not all tabs)
        auto currentId = TabManager::instance().currentTargetId();

        if (!currentId.empty())
        {
            auto *failedTab = TabManager::instance().getTab(currentId);

            if (failedTab)
            {
                failedTab->disconnect();
            }
        }

        return "Error: action '" + action + "' failed: " + std::string(e.what()) +
               ". If this persists, try action='stop' then action='start' to restart the browser.";
    }
}

ToolSchema BrowserTool::schema() const
{
    return {
        "browser",
        "Control a Chrome browser via CDP. Use action='navigate' as the primary way to go to a URL "
        "(it auto-starts the browser and uses the current tab - do NOT use 'open' to visit URLs). "
        "After navigating, use 'snapshot' to read page content or 'inspect' to discover interactive elements. "
        "Supports: navigate/back/forward/reload/scroll, click/type/press/hover/drag/select/fill, "
        "snapshot/screenshot/inspect/pdf, console/errors/requests/response_body, evaluate, "
        "cookies/storage, device/viewport/geolocation/timezone/locale/media emulation, "
        "set_offline/set_headers/set_credentials, dialog/upload, tab management (tabs/open/focus/close).",
        {{"type", "object"},
         {"properties",
          {
              {"action", {{"type", "string"}, {"enum", nlohmann::json::array({
                                                           "status",
                                                           "start",
                                                           "stop",
                                                           "tabs",
                                                           "open",
                                                           "focus",
                                                           "close",
                                                           "navigate",
                                                           "back",
                                                           "forward",
                                                           "reload",
                                                           "scroll",
                                                           "wait",
                                                           "snapshot",
                                                           "screenshot",
                                                           "inspect",
                                                           "pdf",
                                                           "click",
                                                           "type",
                                                           "press",
                                                           "hover",
                                                           "select",
                                                           "fill",
                                                           "drag",
                                                           "scroll_into_view",
                                                           "resize",
                                                           "evaluate",
                                                           "console",
                                                           "errors",
                                                           "requests",
                                                           "response_body",
                                                           "cookies",
                                                           "set_cookie",
                                                           "clear_cookies",
                                                           "get_storage",
                                                           "set_storage",
                                                           "clear_storage",
                                                           "set_offline",
                                                           "set_headers",
                                                           "set_credentials",
                                                           "set_geolocation",
                                                           "set_media",
                                                           "set_timezone",
                                                           "set_locale",
                                                           "set_device",
                                                           "dialog",
                                                           "upload",
                                                       })},
                          {"description", "Browser action to perform"}}},
              {"url", {{"type", "string"}, {"description", "URL for navigate/open/set_cookie"}}},
              {"selector", {{"type", "string"}, {"description", "CSS selector for element interactions (click, type, hover, select, fill, drag, scroll_into_view, upload, screenshot)"}}},
              {"target_id", {{"type", "string"}, {"description", "Tab target ID for focus/close"}}},
              {"text", {{"type", "string"}, {"description", "Text to type (type action) or text to wait for (wait action)"}}},
              {"key", {{"type", "string"}, {"description", "Key to press: Enter, Tab, Escape, Backspace, Delete, ArrowUp/Down/Left/Right, Home, End, PageUp/Down, Space, F1-F12"}}},
              {"script", {{"type", "string"}, {"description", "JavaScript expression for evaluate action"}}},
              {"seconds", {{"type", "integer"}, {"description", "Wait duration in seconds (max 60)"}}},
              {"direction", {{"type", "string"}, {"description", "Scroll direction: up, down, left, right (default: down)"}}},
              {"amount", {{"type", "integer"}, {"description", "Scroll amount in pixels (default: one viewport height)"}}},
              {"max_chars", {{"type", "integer"}, {"description", "Max characters for snapshot/response_body (default 50000)"}}},
              {"headless", {{"type", "boolean"}, {"description", "Run Chrome in headless mode (default false, browser is visible)"}}},
              {"full_page", {{"type", "boolean"}, {"description", "Capture full page screenshot (default false)"}}},
              {"image_type", {{"type", "string"}, {"description", "Screenshot format: png (default), jpeg, webp"}}},
              {"quality", {{"type", "integer"}, {"description", "JPEG/WebP quality 0-100 (default 80)"}}},
              {"format", {{"type", "string"}, {"description", "Snapshot format: text (default) or accessibility (structured tree with interactive refs)"}}},
              {"interactive", {{"type", "boolean"}, {"description", "Include only interactive elements in accessibility snapshot"}}},
              {"double_click", {{"type", "boolean"}, {"description", "Double-click (default false)"}}},
              {"button", {{"type", "string"}, {"description", "Mouse button: left (default), right, middle"}}},
              {"submit", {{"type", "boolean"}, {"description", "Press Enter after typing (default false)"}}},
              {"slowly", {{"type", "boolean"}, {"description", "Type with 75ms delay between characters (default false)"}}},
              {"values", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Values to select in dropdown"}}},
              {"fields", {{"type", "array"}, {"description", "Array of {selector, value, type?} objects for batch form fill"}}},
              {"end_selector", {{"type", "string"}, {"description", "Drop target CSS selector for drag action"}}},
              {"width", {{"type", "integer"}, {"description", "Viewport/device width for resize/set_device"}}},
              {"height", {{"type", "integer"}, {"description", "Viewport/device height for resize/set_device"}}},
              {"level", {{"type", "string"}, {"description", "Console level filter: log, warn, error, info, debug"}}},
              {"filter", {{"type", "string"}, {"description", "URL substring filter for requests/response_body"}}},
              {"name", {{"type", "string"}, {"description", "Cookie/storage key name"}}},
              {"value", {{"type", "string"}, {"description", "Cookie/storage value"}}},
              {"domain", {{"type", "string"}, {"description", "Cookie domain"}}},
              {"path", {{"type", "string"}, {"description", "Cookie path or file path for upload"}}},
              {"kind", {{"type", "string"}, {"description", "Storage kind: local (default) or session"}}},
              {"enabled", {{"type", "boolean"}, {"description", "Toggle for set_offline (default true)"}}},
              {"headers", {{"type", "object"}, {"description", "HTTP headers object for set_headers"}}},
              {"username", {{"type", "string"}, {"description", "HTTP auth username"}}},
              {"password", {{"type", "string"}, {"description", "HTTP auth password"}}},
              {"latitude", {{"type", "number"}, {"description", "Geolocation latitude"}}},
              {"longitude", {{"type", "number"}, {"description", "Geolocation longitude"}}},
              {"media", {{"type", "string"}, {"description", "Color scheme: dark, light, no-preference, none (clear)"}}},
              {"timezone", {{"type", "string"}, {"description", "IANA timezone ID (e.g. America/New_York)"}}},
              {"locale", {{"type", "string"}, {"description", "Locale code (e.g. en-US, pt-BR)"}}},
              {"device", {{"type", "string"}, {"description", "Device preset: iPhone 14, iPhone 14 Pro Max, iPhone SE, iPad, iPad Pro, Pixel 7, Samsung Galaxy S23, Desktop 1080p, Desktop 1440p"}}},
              {"accept", {{"type", "boolean"}, {"description", "Accept (true) or dismiss (false) dialog"}}},
              {"output_path", {{"type", "string"}, {"description", "Absolute file path to save screenshot/PDF. Parent directories are created automatically. Defaults to temp directory if not specified."}}},
              {"prompt_text", {{"type", "string"}, {"description", "Text to enter in prompt dialog"}}},
              {"wait_selector", {{"type", "string"}, {"description", "CSS selector to wait for visibility"}}},
              {"wait_url", {{"type", "string"}, {"description", "URL glob pattern to wait for"}}},
              {"wait_fn", {{"type", "string"}, {"description", "JavaScript predicate to wait for (must return truthy)"}}},
              {"clear", {{"type", "boolean"}, {"description", "Clear data after retrieval (console, errors, requests) or clear override (credentials, geolocation, device)"}}},
          }},
         {"required", nlohmann::json::array({"action"})}}};
}

std::set<std::string> BrowserTool::supportedPlatforms() const
{
    return {"linux", "macos", "windows"};
}

} // namespace builtin
} // namespace tool
} // namespace ionclaw
