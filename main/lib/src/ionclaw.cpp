#include "ionclaw/ionclaw.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <unordered_map>

#include "ionclaw/platform/PlatformBridge.hpp"
#include "ionclaw/server/ServerInstance.hpp"
#include "ionclaw/util/EmbeddedResources.hpp"
#include "nlohmann/json.hpp"

namespace
{

char *stringToMalloc(const std::string &str)
{
    auto *copy = static_cast<char *>(std::malloc(str.size() + 1));

    if (!copy)
    {
        return nullptr;
    }

    std::memcpy(copy, str.c_str(), str.size() + 1);
    return copy;
}

char *jsonToMalloc(const nlohmann::json &j)
{
    // replace invalid utf-8 instead of throwing, since an exception must never cross the c boundary
    return stringToMalloc(j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
}

struct PendingRequest
{
    std::mutex mutex;
    std::condition_variable cv;
    std::string result;
    bool ready = false;
};

std::mutex g_requestsMutex;
std::unordered_map<int64_t, std::shared_ptr<PendingRequest>> g_pendingRequests;
std::atomic<int64_t> g_nextRequestId{1};
std::atomic<int> g_platformTimeoutSeconds{30};

void abortPendingPlatformRequests()
{
    // release any thread parked in a platform callback so a stop does not wait out the timeout
    std::lock_guard<std::mutex> lock(g_requestsMutex);

    for (auto &[id, pending] : g_pendingRequests)
    {
        {
            std::lock_guard<std::mutex> pendingLock(pending->mutex);
            pending->result = "Error: server is stopping";
            pending->ready = true;
        }
        pending->cv.notify_one();
    }

    g_pendingRequests.clear();
}

} // namespace

extern "C"
{

    const char *ionclaw_project_init(const char *path)
    {
        try
        {
            std::string targetDir = path ? path : "";

            if (targetDir.empty())
            {
                return jsonToMalloc({{"success", false}, {"error", "path is required"}});
            }

            std::error_code ec;
            std::filesystem::create_directories(targetDir, ec);
            targetDir = std::filesystem::absolute(targetDir).string();

            if (std::filesystem::exists(targetDir + "/config.yml"))
            {
                return jsonToMalloc({{"success", true}, {"message", "already initialized"}});
            }

            bool success = ionclaw::util::EmbeddedResources::extractTemplate(targetDir);

            if (!success)
            {
                return jsonToMalloc({{"success", false}, {"error", "failed to extract template"}});
            }

            // orchestrator state lives at the project root; the agent workspace holds the agent's own files, including public/
            std::filesystem::create_directories(targetDir + "/sessions", ec);
            std::filesystem::create_directories(targetDir + "/workspace/skills", ec);
            std::filesystem::create_directories(targetDir + "/workspace/memory", ec);
            std::filesystem::create_directories(targetDir + "/workspace/public", ec);
            std::filesystem::create_directories(targetDir + "/skills", ec);

            return jsonToMalloc({{"success", true}});
        }
        catch (const std::exception &e)
        {
            return jsonToMalloc({{"success", false}, {"error", e.what()}});
        }
        catch (...)
        {
            return stringToMalloc(R"({"success":false,"error":"internal error"})");
        }
    }

    const char *ionclaw_server_start(const char *project_path, const char *host, int port, const char *root_path, const char *web_path)
    {
        try
        {
            auto result = ionclaw::server::ServerInstance::start(
                project_path ? project_path : "",
                host ? host : "",
                port,
                root_path ? root_path : "",
                web_path ? web_path : "");

            nlohmann::json j = {
                {"host", result.host},
                {"port", result.port},
                {"success", result.success},
            };

            if (!result.error.empty())
            {
                j["error"] = result.error;
            }

            return jsonToMalloc(j);
        }
        catch (...)
        {
            return stringToMalloc(R"({"success":false,"error":"internal error starting server"})");
        }
    }

    const char *ionclaw_server_stop(void)
    {
        try
        {
            // unblock the worker before joining, then drop the handler so no static from this unit is touched after teardown
            abortPendingPlatformRequests();
            auto result = ionclaw::server::ServerInstance::stop();
            ionclaw::platform::PlatformBridge::instance().clearHandler();

            nlohmann::json j = {
                {"success", result.success},
            };

            if (!result.error.empty())
            {
                j["error"] = result.error;
            }

            return jsonToMalloc(j);
        }
        catch (...)
        {
            return stringToMalloc(R"({"success":false,"error":"internal error stopping server"})");
        }
    }

    void ionclaw_set_platform_handler(ionclaw_platform_callback_t callback, int timeout_seconds)
    {
        try
        {
            if (!callback)
            {
                return;
            }

            g_platformTimeoutSeconds = timeout_seconds > 0 ? timeout_seconds : 30;

            // clang-format off
            ionclaw::platform::PlatformBridge::instance().setHandler(
                [callback](const std::string &function, const nlohmann::json &params) -> std::string {
                    auto requestId = g_nextRequestId.fetch_add(1);
                    auto pending = std::make_shared<PendingRequest>();

                    {
                        std::lock_guard<std::mutex> lock(g_requestsMutex);
                        g_pendingRequests[requestId] = pending;
                    }

                    auto paramsStr = params.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
                    callback(requestId, function.c_str(), paramsStr.c_str());

                    std::string response;
                    {
                        std::unique_lock<std::mutex> lock(pending->mutex);
                        if (pending->cv.wait_for(lock, std::chrono::seconds(g_platformTimeoutSeconds), [&] { return pending->ready; }))
                        {
                            response = pending->result;
                        }
                        else
                        {
                            response = "Error: platform handler timed out after " + std::to_string(g_platformTimeoutSeconds) + "s";
                        }
                    }

                    {
                        std::lock_guard<std::mutex> lock(g_requestsMutex);
                        g_pendingRequests.erase(requestId);
                    }

                    return response;
                });
            // clang-format on
        }
        catch (...)
        {
        }
    }

    void ionclaw_platform_respond(int64_t request_id, const char *result)
    {
        try
        {
            std::shared_ptr<PendingRequest> pending;

            {
                std::lock_guard<std::mutex> lock(g_requestsMutex);
                auto it = g_pendingRequests.find(request_id);

                if (it == g_pendingRequests.end())
                {
                    return;
                }

                pending = it->second;
            }

            {
                std::lock_guard<std::mutex> lock(pending->mutex);
                pending->result = result ? result : "";
                pending->ready = true;
            }

            pending->cv.notify_one();
        }
        catch (...)
        {
        }
    }

    void ionclaw_free(const char *ptr)
    {
        std::free(const_cast<char *>(ptr));
    }

} // extern "C"
