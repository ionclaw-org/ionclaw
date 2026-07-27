#include "doctest/doctest.h"

#include <filesystem>
#include <memory>
#include <thread>
#include <vector>

#include "ionclaw/bus/MessageBus.hpp"
#include "ionclaw/heartbeat/HeartbeatService.hpp"
#include "ionclaw/session/SessionManager.hpp"
#include "ionclaw/util/RandomHelper.hpp"

using namespace ionclaw;
namespace fs = std::filesystem;

namespace
{

fs::path makeDir(const char *prefix)
{
    auto dir = fs::temp_directory_path() / (std::string(prefix) + ionclaw::util::RandomHelper::secureHex(8));
    fs::create_directories(dir);
    return dir;
}

} // namespace

TEST_CASE("concurrent start, stop and restart never terminate the process")
{
    auto sessions = makeDir("ionclaw-hb-sessions-");
    auto workspace = makeDir("ionclaw-hb-ws-");

    auto bus = std::make_shared<bus::MessageBus>();
    auto sessionManager = std::make_shared<session::SessionManager>(sessions.string());

    heartbeat::HeartbeatService service(bus, sessionManager, workspace.string(), 1, true, "main");
    service.start();

    constexpr int threads = 4;
    constexpr int perThread = 5;
    std::vector<std::thread> workers;

    for (int t = 0; t < threads; ++t)
    {
        // clang-format off
        workers.emplace_back([&service, t] {
            for (int i = 0; i < perThread; ++i)
            {
                if ((t + i) % 3 == 0)
                {
                    service.restart(1, true, "main");
                }
                else if ((t + i) % 3 == 1)
                {
                    service.stop();
                }
                else
                {
                    service.start();
                }
            }
        });
        // clang-format on
    }

    for (auto &w : workers)
    {
        w.join();
    }

    service.stop();

    // reaching here without a std::terminate is the whole point: the lifecycle is serialized
    CHECK(true);

    fs::remove_all(sessions);
    fs::remove_all(workspace);
}
