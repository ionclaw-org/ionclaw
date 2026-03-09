#include "ionclaw/tool/builtin/BrowserTool.hpp"

#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

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

// chrome instance singleton
class ChromeInstance
{
public:
    static ChromeInstance &instance()
    {
        static ChromeInstance inst;
        return inst;
    }

    bool isRunning() const { return pid > 0; }

    std::string launch(bool headless)
    {
        std::lock_guard<std::mutex> lock(mtx);

        if (pid > 0)
        {
            return "";
        }

        auto chromePath = findChrome();

        if (chromePath.empty())
        {
            return "Error: Chrome not found. Install Google Chrome to use the browser tool.";
        }

        // build user data dir
        std::string userDataDir;
        auto homeDir = std::getenv("HOME");

        if (homeDir)
        {
            userDataDir = std::string(homeDir) + "/.ionclaw/chrome-profile";
        }
        else
        {
            userDataDir = "/tmp/ionclaw-chrome-profile";
        }

        std::ostringstream cmd;
        cmd << "\"" << chromePath << "\""
            << " --remote-debugging-port=" << BrowserTool::CDP_PORT
            << " --user-data-dir=\"" << userDataDir << "\""
            << " --no-first-run"
            << " --no-default-browser-check"
            << " --disable-background-networking"
            << " --disable-sync";

        if (headless)
        {
            cmd << " --headless=new";
        }

        cmd << " about:blank";

#ifdef _WIN32
        auto cmdStr = cmd.str() + " &";
        auto result = std::system(cmdStr.c_str());

        if (result != 0)
        {
            return "Error: failed to launch Chrome";
        }

        pid = 1;
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
            return "Error: failed to launch Chrome";
        }

        pid = childPid;
#endif

        // wait for cdp to be ready
        for (int i = 0; i < 30; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));

            try
            {
                auto response = ionclaw::util::HttpClient::request(
                    "GET", "http://127.0.0.1:" + std::to_string(BrowserTool::CDP_PORT) + "/json", {}, "", 2, false);

                if (response.statusCode == 200)
                {
                    return "";
                }
            }
            catch (...)
            {
            }
        }

        return "Error: Chrome launched but CDP endpoint not responding";
    }

    void shutdown()
    {
        std::lock_guard<std::mutex> lock(mtx);

        if (pid > 0)
        {
#ifdef _WIN32
            std::system("taskkill /F /IM chrome.exe > nul 2>&1");
#elif !defined(__ANDROID__)
            kill(static_cast<pid_t>(pid), SIGTERM);
#endif
            pid = 0;
        }
    }

private:
    ChromeInstance() = default;
    ~ChromeInstance() { shutdown(); }

    std::string findChrome()
    {
#ifdef __APPLE__
        return "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome";
#elif defined(_WIN32)
        // check common windows paths
        const char *paths[] = {
            "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
            "C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe",
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
        // linux: try common paths
        const char *paths[] = {
            "/usr/bin/google-chrome",
            "/usr/bin/google-chrome-stable",
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

    int pid = 0;
    std::mutex mtx;
};

// cdp client for websocket communication
class CdpClient
{
public:
    CdpClient() = default;

    std::string connect()
    {
        try
        {
            // discover debugger websocket url
            auto response = ionclaw::util::HttpClient::request(
                "GET", "http://127.0.0.1:" + std::to_string(BrowserTool::CDP_PORT) + "/json", {}, "", 5, false);

            if (response.statusCode != 200)
            {
                return "Error: cannot discover CDP targets";
            }

            auto targets = nlohmann::json::parse(response.body);

            if (targets.empty())
            {
                return "Error: no CDP targets available";
            }

            // find first page target
            std::string wsUrl;

            for (const auto &target : targets)
            {
                if (target.value("type", "") == "page")
                {
                    wsUrl = target.value("webSocketDebuggerUrl", "");

                    if (!wsUrl.empty())
                    {
                        break;
                    }
                }
            }

            if (wsUrl.empty())
            {
                wsUrl = targets[0].value("webSocketDebuggerUrl", "");
            }

            if (wsUrl.empty())
            {
                return "Error: no WebSocket URL in CDP targets";
            }

            // parse websocket url and connect
            Poco::URI uri(wsUrl);
            session = std::make_unique<Poco::Net::HTTPClientSession>(uri.getHost(), uri.getPort());
            session->setTimeout(Poco::Timespan(BrowserTool::CDP_TIMEOUT_SECONDS, 0));

            Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_GET, uri.getPathAndQuery(), Poco::Net::HTTPMessage::HTTP_1_1);
            request.set("Host", uri.getHost());

            Poco::Net::HTTPResponse httpResponse;
            ws = std::make_unique<Poco::Net::WebSocket>(*session, request, httpResponse);
            ws->setReceiveTimeout(Poco::Timespan(BrowserTool::CDP_TIMEOUT_SECONDS, 0));

            connected = true;
            return "";
        }
        catch (const std::exception &e)
        {
            return "Error: CDP connection failed: " + std::string(e.what());
        }
    }

    nlohmann::json sendCommand(const std::string &method, const nlohmann::json &cmdParams = nlohmann::json::object())
    {
        if (!connected || !ws)
        {
            throw std::runtime_error("CDP not connected");
        }

        auto id = nextId++;

        nlohmann::json message = {
            {"id", id},
            {"method", method},
            {"params", cmdParams},
        };

        auto msgStr = message.dump();
        ws->sendFrame(msgStr.data(), static_cast<int>(msgStr.size()), Poco::Net::WebSocket::FRAME_TEXT);

        // receive response
        char buffer[65536];
        int flags;

        while (true)
        {
            auto n = ws->receiveFrame(buffer, sizeof(buffer), flags);

            if (n <= 0)
            {
                throw std::runtime_error("CDP connection closed");
            }

            auto response = nlohmann::json::parse(std::string(buffer, n));

            if (response.contains("id") && response["id"].get<int>() == id)
            {
                if (response.contains("error"))
                {
                    throw std::runtime_error("CDP error: " + response["error"].value("message", "unknown"));
                }

                return response.value("result", nlohmann::json::object());
            }

            // handle events (skip them and continue waiting for our response)
        }
    }

    void waitForEvent(const std::string &eventName, int timeoutMs = 10000)
    {
        if (!connected || !ws)
        {
            return;
        }

        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        char buffer[65536];
        int flags;

        while (std::chrono::steady_clock::now() < deadline)
        {
            try
            {
                ws->setReceiveTimeout(Poco::Timespan(0, 500000)); // 500ms

                auto n = ws->receiveFrame(buffer, sizeof(buffer), flags);

                if (n > 0)
                {
                    auto msg = nlohmann::json::parse(std::string(buffer, n));

                    if (msg.contains("method") && msg["method"].get<std::string>() == eventName)
                    {
                        return;
                    }
                }
            }
            catch (const Poco::TimeoutException &)
            {
                // continue waiting
            }
        }

        // reset timeout
        ws->setReceiveTimeout(Poco::Timespan(BrowserTool::CDP_TIMEOUT_SECONDS, 0));
    }

    bool isConnected() const { return connected; }

private:
    std::unique_ptr<Poco::Net::HTTPClientSession> session;
    std::unique_ptr<Poco::Net::WebSocket> ws;
    bool connected = false;
    int nextId = 1;
};

// reusable cdp client singleton
CdpClient &BrowserTool::getCdpClient()
{
    static CdpClient client;
    return client;
}

// launch browser and connect cdp if needed
std::string BrowserTool::ensureBrowser(bool headless)
{
    auto &chrome = ChromeInstance::instance();

    if (!chrome.isRunning())
    {
        auto err = chrome.launch(headless);

        if (!err.empty())
        {
            return err;
        }
    }

    auto &cdp = getCdpClient();

    if (!cdp.isConnected())
    {
        auto err = cdp.connect();

        if (!err.empty())
        {
            return err;
        }

        // enable required domains
        cdp.sendCommand("Page.enable");
        cdp.sendCommand("Runtime.enable");
        cdp.sendCommand("DOM.enable");
        cdp.sendCommand("Input.enable");
    }

    return "";
}

// execute browser action
std::string BrowserTool::execute(const nlohmann::json &params, const ToolContext &context)
{
    auto action = params.at("action").get<std::string>();

    bool headless = true;

    if (params.contains("headless") && params["headless"].is_boolean())
    {
        headless = params["headless"].get<bool>();
    }

    // ensure browser is running
    auto err = ensureBrowser(headless);

    if (!err.empty())
    {
        return err;
    }

    auto &cdp = getCdpClient();

    try
    {
        // navigate action
        if (action == "navigate")
        {
            if (!params.contains("url") || !params["url"].is_string())
            {
                return "Error: 'url' is required for navigate action";
            }

            auto url = params["url"].get<std::string>();
            cdp.sendCommand("Page.navigate", {{"url", url}});
            cdp.waitForEvent("Page.loadEventFired", 30000);

            return "Navigated to: " + url;
        }

        // snapshot action
        else if (action == "snapshot")
        {
            int maxChars = 50000;

            if (params.contains("max_chars") && params["max_chars"].is_number_integer())
            {
                maxChars = params["max_chars"].get<int>();
            }

            auto result = cdp.sendCommand("Runtime.evaluate", {
                                                                  {"expression", "document.body.innerText"},
                                                                  {"returnByValue", true},
                                                              });

            auto text = result["result"].value("value", "");

            if (static_cast<int>(text.size()) > maxChars)
            {
                text = ionclaw::util::StringHelper::utf8SafeTruncate(text, maxChars) + "\n... [truncated]";
            }

            return text;
        }

        // screenshot action
        else if (action == "screenshot")
        {
            auto result = cdp.sendCommand("Page.captureScreenshot", {
                                                                        {"format", "png"},
                                                                    });

            auto data = result.value("data", "");

            if (data.empty())
            {
                return "Error: no screenshot data";
            }

            return "data:image/png;base64," + data;
        }

        // click action
        else if (action == "click")
        {
            if (!params.contains("selector") || !params["selector"].is_string())
            {
                return "Error: 'selector' is required for click action";
            }

            auto selector = params["selector"].get<std::string>();
            auto safeSelector = ToolHelper::escapeForJs(selector);

            // find element and get its center position
            auto evalResult = cdp.sendCommand("Runtime.evaluate", {
                                                                      {"expression", "(() => { const el = document.querySelector('" + safeSelector + "'); if (!el) return null; const r = el.getBoundingClientRect(); return {x: r.x + r.width/2, y: r.y + r.height/2}; })()"},
                                                                      {"returnByValue", true},
                                                                  });

            auto value = evalResult["result"].value("value", nlohmann::json());

            if (value.is_null() || !value.contains("x") || !value.contains("y") ||
                !value["x"].is_number() || !value["y"].is_number())
            {
                return "Error: element not found or missing coordinates: " + selector;
            }

            auto x = value["x"].get<double>();
            auto y = value["y"].get<double>();

            cdp.sendCommand("Input.dispatchMouseEvent", {
                                                            {"type", "mousePressed"},
                                                            {"x", x},
                                                            {"y", y},
                                                            {"button", "left"},
                                                            {"clickCount", 1},
                                                        });

            cdp.sendCommand("Input.dispatchMouseEvent", {
                                                            {"type", "mouseReleased"},
                                                            {"x", x},
                                                            {"y", y},
                                                            {"button", "left"},
                                                            {"clickCount", 1},
                                                        });

            return "Clicked: " + selector;
        }

        // type action
        else if (action == "type")
        {
            if (!params.contains("text") || !params["text"].is_string())
            {
                return "Error: 'text' is required for type action";
            }

            auto text = params["text"].get<std::string>();

            // optionally click on selector first
            if (params.contains("selector") && params["selector"].is_string())
            {
                auto selector = params["selector"].get<std::string>();
                auto safeSelector = ToolHelper::escapeForJs(selector);
                auto evalResult = cdp.sendCommand("Runtime.evaluate", {
                                                                          {"expression", "(() => { const el = document.querySelector('" + safeSelector + "'); if (!el) return null; el.focus(); const r = el.getBoundingClientRect(); return {x: r.x + r.width/2, y: r.y + r.height/2}; })()"},
                                                                          {"returnByValue", true},
                                                                      });

                auto value = evalResult["result"].value("value", nlohmann::json());

                if (value.is_null() || !value.contains("x") || !value.contains("y") ||
                    !value["x"].is_number() || !value["y"].is_number())
                {
                    return "Error: element not found or missing coordinates: " + selector;
                }

                auto x = value["x"].get<double>();
                auto y = value["y"].get<double>();

                cdp.sendCommand("Input.dispatchMouseEvent", {{"type", "mousePressed"}, {"x", x}, {"y", y}, {"button", "left"}, {"clickCount", 1}});
                cdp.sendCommand("Input.dispatchMouseEvent", {{"type", "mouseReleased"}, {"x", x}, {"y", y}, {"button", "left"}, {"clickCount", 1}});
            }

            // type each character
            for (char c : text)
            {
                std::string ch(1, c);
                cdp.sendCommand("Input.dispatchKeyEvent", {
                                                              {"type", "keyDown"},
                                                              {"text", ch},
                                                          });

                cdp.sendCommand("Input.dispatchKeyEvent", {
                                                              {"type", "keyUp"},
                                                              {"text", ch},
                                                          });
            }

            return "Typed: " + text;
        }

        // press action
        else if (action == "press")
        {
            if (!params.contains("key") || !params["key"].is_string())
            {
                return "Error: 'key' is required for press action";
            }

            auto key = params["key"].get<std::string>();

            cdp.sendCommand("Input.dispatchKeyEvent", {
                                                          {"type", "keyDown"},
                                                          {"key", key},
                                                      });

            cdp.sendCommand("Input.dispatchKeyEvent", {
                                                          {"type", "keyUp"},
                                                          {"key", key},
                                                      });

            return "Pressed: " + key;
        }

        // inspect action
        else if (action == "inspect")
        {
            auto js = R"(
                (() => {
                    const elements = [];
                    const selectors = 'a[href], button, input, select, textarea, [role="button"], [onclick], [data-testid]';
                    const nodes = document.querySelectorAll(selectors);

                    for (let i = 0; i < Math.min(nodes.length, )" +
                      std::to_string(MAX_INTERACTIVE_ELEMENTS) + R"(); i++) {
                        const el = nodes[i];
                        const r = el.getBoundingClientRect();
                        if (r.width === 0 && r.height === 0) continue;

                        let selector = '';
                        if (el.id) selector = '#' + el.id;
                        else if (el.getAttribute('data-testid')) selector = '[data-testid="' + el.getAttribute('data-testid') + '"]';
                        else if (el.getAttribute('aria-label')) selector = '[aria-label="' + el.getAttribute('aria-label') + '"]';
                        else selector = el.tagName.toLowerCase() + (el.className ? '.' + el.className.split(' ').join('.') : '');

                        elements.push({
                            tag: el.tagName.toLowerCase(),
                            selector: selector,
                            text: (el.innerText || el.value || el.placeholder || '').substring(0, 100),
                            type: el.type || '',
                            href: el.href || ''
                        });
                    }
                    return elements;
                })()
            )";

            auto result = cdp.sendCommand("Runtime.evaluate", {
                                                                  {"expression", js},
                                                                  {"returnByValue", true},
                                                              });

            auto elements = result["result"].value("value", nlohmann::json::array());

            if (elements.empty())
            {
                return "No interactive elements found on the page.";
            }

            std::ostringstream output;
            output << "Interactive elements (" << elements.size() << "):\n\n";

            for (const auto &el : elements)
            {
                output << "- " << el.value("tag", "") << " | " << el.value("selector", "")
                       << " | " << el.value("text", "");

                if (!el.value("href", "").empty())
                {
                    output << " | href=" << el.value("href", "");
                }

                output << "\n";
            }

            return output.str();
        }

        // evaluate action
        else if (action == "evaluate")
        {
            if (!params.contains("script") || !params["script"].is_string())
            {
                return "Error: 'script' is required for evaluate action";
            }

            auto script = params["script"].get<std::string>();

            auto result = cdp.sendCommand("Runtime.evaluate", {
                                                                  {"expression", script},
                                                                  {"returnByValue", true},
                                                              });

            auto value = result["result"];

            if (value.contains("value"))
            {
                if (value["value"].is_string())
                {
                    return value["value"].get<std::string>();
                }
                else
                {
                    return value["value"].dump(2);
                }
            }

            return value.dump(2);
        }

        // wait action
        else if (action == "wait")
        {
            int seconds = 1;

            if (params.contains("seconds") && params["seconds"].is_number_integer())
            {
                seconds = std::max(1, std::min(30, params["seconds"].get<int>()));
            }

            std::this_thread::sleep_for(std::chrono::seconds(seconds));
            return "Waited " + std::to_string(seconds) + " seconds.";
        }

        return "Error: unknown action: " + action;
    }
    catch (const std::exception &e)
    {
        // on cdp error, reset connection for next attempt
        getCdpClient() = CdpClient();
        return "Error: browser action failed: " + std::string(e.what());
    }
}

// schema definition
ToolSchema BrowserTool::schema() const
{
    return {
        "browser",
        "Control a Chrome browser via Chrome DevTools Protocol. Supports navigation, page content extraction, screenshots, element interaction, and JavaScript evaluation.",
        {{"type", "object"},
         {"properties",
          {{"action", {{"type", "string"}, {"enum", nlohmann::json::array({"navigate", "snapshot", "screenshot", "click", "type", "press", "inspect", "evaluate", "wait"})}, {"description", "The browser action to perform"}}},
           {"url", {{"type", "string"}, {"description", "URL to navigate to (for navigate action)"}}},
           {"selector", {{"type", "string"}, {"description", "CSS selector for element interaction (click, type)"}}},
           {"text", {{"type", "string"}, {"description", "Text to type (for type action)"}}},
           {"key", {{"type", "string"}, {"description", "Key to press (for press action, e.g. 'Enter', 'Tab')"}}},
           {"script", {{"type", "string"}, {"description", "JavaScript to evaluate (for evaluate action)"}}},
           {"seconds", {{"type", "integer"}, {"description", "Seconds to wait (for wait action, max 30)"}}},
           {"max_chars", {{"type", "integer"}, {"description", "Max characters for snapshot (default 50000)"}}},
           {"headless", {{"type", "boolean"}, {"description", "Run Chrome in headless mode (default true)"}}}}},
         {"required", nlohmann::json::array({"action"})}}};
}

// supported platforms
std::set<std::string> BrowserTool::supportedPlatforms() const
{
    return {"linux", "macos", "windows"};
}

} // namespace builtin
} // namespace tool
} // namespace ionclaw
