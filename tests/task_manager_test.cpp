#include "doctest/doctest.h"

#include <atomic>
#include <filesystem>
#include <thread>
#include <vector>

#include "ionclaw/task/TaskManager.hpp"
#include "ionclaw/util/RandomHelper.hpp"

using namespace ionclaw::task;
namespace fs = std::filesystem;

namespace
{

fs::path makeTasksFile()
{
    auto dir = fs::temp_directory_path() / ("ionclaw-tasks-" + ionclaw::util::RandomHelper::secureHex(8));
    fs::create_directories(dir);
    return dir / "tasks.json";
}

} // namespace

TEST_CASE("a created task round-trips through save and load")
{
    auto file = makeTasksFile();

    {
        TaskManager mgr(file.string(), nullptr);
        auto task = mgr.createTask("title", "description", "web", "chat-1");
        mgr.updateState(task.id, TaskState::Done, "finished");
        mgr.save();

        auto reloaded = mgr.getTask(task.id);
        CHECK(reloaded.title == "title");
        CHECK(reloaded.state == TaskState::Done);
        CHECK(reloaded.result == "finished");
    }

    TaskManager fresh(file.string(), nullptr);
    fresh.load();
    CHECK(fresh.listTasks().size() == 1);

    fs::remove_all(file.parent_path());
}

TEST_CASE("concurrent task creation persists every task without corruption")
{
    auto file = makeTasksFile();

    constexpr int threads = 8;
    constexpr int perThread = 25;

    {
        TaskManager mgr(file.string(), nullptr);
        std::vector<std::thread> workers;
        std::atomic<int> created{0};

        for (int t = 0; t < threads; ++t)
        {
            // clang-format off
            workers.emplace_back([&mgr, &created, t] {
                for (int i = 0; i < perThread; ++i)
                {
                    mgr.createTask("t" + std::to_string(t) + "-" + std::to_string(i), "d", "web", "chat");
                    created.fetch_add(1);
                }
            });
            // clang-format on
        }

        for (auto &w : workers)
        {
            w.join();
        }

        CHECK(created.load() == threads * perThread);
        CHECK(mgr.listTasks().size() == static_cast<size_t>(threads * perThread));
        mgr.save();
    }

    // a fresh manager sees every persisted task, so no concurrent append was lost or corrupted
    TaskManager fresh(file.string(), nullptr);
    fresh.load();
    CHECK(fresh.listTasks().size() == static_cast<size_t>(threads * perThread));

    fs::remove_all(file.parent_path());
}

TEST_CASE("save while other threads create tasks keeps the file loadable and complete")
{
    auto file = makeTasksFile();
    constexpr int total = 200;

    {
        TaskManager mgr(file.string(), nullptr);
        std::atomic<bool> stop{false};

        // clang-format off
        std::thread saver([&mgr, &stop] {
            while (!stop.load())
            {
                mgr.save();
            }
        });
        // clang-format on

        for (int i = 0; i < total; ++i)
        {
            mgr.createTask("task-" + std::to_string(i), "d", "web", "chat");
        }

        stop.store(true);
        saver.join();
        mgr.save();
    }

    TaskManager fresh(file.string(), nullptr);
    fresh.load();
    CHECK(fresh.listTasks().size() == static_cast<size_t>(total));

    fs::remove_all(file.parent_path());
}
