#include "doctest/doctest.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <thread>
#include <vector>

#include "ionclaw/agent/SubagentRegistry.hpp"
#include "ionclaw/util/RandomHelper.hpp"

using namespace ionclaw::agent;
namespace fs = std::filesystem;

namespace
{

fs::path makeTempDir()
{
    auto dir = fs::temp_directory_path() / ("ionclaw-subagent-" + ionclaw::util::RandomHelper::secureHex(8));
    fs::create_directories(dir);
    return dir;
}

} // namespace

TEST_CASE("spawn records a child and tracks depth and active count")
{
    auto dir = makeTempDir();
    SubagentRegistry reg(dir.string());

    auto rec = reg.spawn("root", "do work", "root/child-1", "", "", 0, 300);

    CHECK(reg.getActiveChildCount("root") == 1);
    CHECK(reg.getDepth("root/child-1") == 1);
    CHECK(reg.getChildren("root").size() == 1);

    reg.updateStatus(rec.runId, SubagentStatus::Completed, "done");
    CHECK(reg.getActiveChildCount("root") == 0);
    CHECK(reg.allChildrenTerminal("root"));

    fs::remove_all(dir);
}

TEST_CASE("descendant walk terminates on a cyclic corrupted state file")
{
    auto dir = makeTempDir();

    // craft a requester/child cycle that a naive walk would follow forever
    nlohmann::json runs = nlohmann::json::array();
    runs.push_back({{"run_id", "r1"}, {"requester_session_key", "s1"}, {"child_session_key", "s2"}, {"status", "active"}, {"depth", 1}});
    runs.push_back({{"run_id", "r2"}, {"requester_session_key", "s2"}, {"child_session_key", "s1"}, {"status", "active"}, {"depth", 2}});

    std::ofstream(dir / "subagent-runs.json") << runs.dump();

    SubagentRegistry reg(dir.string());
    reg.load();

    // clang-format off
    auto walk = std::async(std::launch::async, [&reg] {
        return reg.getDescendantSessionKeys("s1").size();
    });
    // clang-format on

    REQUIRE(walk.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    CHECK(walk.get() >= 1);

    fs::remove_all(dir);
}

TEST_CASE("concurrent spawns and queries stay consistent")
{
    auto dir = makeTempDir();
    SubagentRegistry reg(dir.string());

    constexpr int threads = 8;
    constexpr int perThread = 20;
    std::vector<std::thread> workers;
    std::atomic<int> spawned{0};

    for (int t = 0; t < threads; ++t)
    {
        // clang-format off
        workers.emplace_back([&reg, &spawned, t] {
            for (int i = 0; i < perThread; ++i)
            {
                auto child = "req-" + std::to_string(t) + "/child-" + std::to_string(i);
                reg.spawn("req-" + std::to_string(t), "task", child, "", "", 0, 300);
                spawned.fetch_add(1);
                reg.getActiveChildCount("req-" + std::to_string(t));
                reg.getChildren("req-" + std::to_string(t));
            }
        });
        // clang-format on
    }

    for (auto &w : workers)
    {
        w.join();
    }

    CHECK(spawned.load() == threads * perThread);

    int total = 0;
    for (int t = 0; t < threads; ++t)
    {
        total += reg.getActiveChildCount("req-" + std::to_string(t));
    }

    CHECK(total == threads * perThread);

    fs::remove_all(dir);
}
