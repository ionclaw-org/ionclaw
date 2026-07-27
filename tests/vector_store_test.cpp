#include "doctest/doctest.h"

#include <filesystem>
#include <vector>

#include "ionclaw/embedding/VectorStore.hpp"
#include "ionclaw/util/RandomHelper.hpp"

using namespace ionclaw::embedding;
namespace fs = std::filesystem;

namespace
{

fs::path makeStoreDir()
{
    auto dir = fs::temp_directory_path() / ("ionclaw-vec-" + ionclaw::util::RandomHelper::secureHex(8));
    fs::create_directories(dir);
    return dir;
}

} // namespace

TEST_CASE("search returns the nearest vector by cosine similarity")
{
    auto dir = makeStoreDir();
    VectorStore store(dir.string(), "index");
    store.open("test-model");

    store.add("a", "alpha", {1.0f, 0.0f, 0.0f, 0.0f});
    store.add("b", "beta", {0.0f, 1.0f, 0.0f, 0.0f});
    store.add("c", "gamma", {0.0f, 0.0f, 1.0f, 0.0f});

    auto results = store.search({0.9f, 0.1f, 0.0f, 0.0f}, 1);
    REQUIRE(results.size() == 1);
    CHECK(results[0].id == "a");
    CHECK(results[0].text == "alpha");

    fs::remove_all(dir);
}

TEST_CASE("a persisted index is fully reloaded by a fresh store")
{
    auto dir = makeStoreDir();

    {
        VectorStore store(dir.string(), "index");
        store.open("test-model");
        store.add("a", "alpha", {1.0f, 0.0f, 0.0f, 0.0f});
        store.add("b", "beta", {0.0f, 1.0f, 0.0f, 0.0f});
        store.save();
    }

    // regression: reload once dropped every entry, so semantic search silently returned nothing after a restart
    VectorStore reopened(dir.string(), "index");
    reopened.open("test-model");

    auto results = reopened.search({0.0f, 1.0f, 0.0f, 0.0f}, 2);
    REQUIRE(results.size() >= 1);
    CHECK(results[0].id == "b");

    fs::remove_all(dir);
}

TEST_CASE("re-adding an id replaces its vector")
{
    auto dir = makeStoreDir();
    VectorStore store(dir.string(), "index");
    store.open("test-model");

    store.add("x", "first", {1.0f, 0.0f, 0.0f, 0.0f});
    store.add("x", "second", {0.0f, 1.0f, 0.0f, 0.0f});

    auto results = store.search({0.0f, 1.0f, 0.0f, 0.0f}, 5);
    REQUIRE(results.size() == 1);
    CHECK(results[0].id == "x");
    CHECK(results[0].text == "second");

    fs::remove_all(dir);
}

TEST_CASE("remove drops an entry from search")
{
    auto dir = makeStoreDir();
    VectorStore store(dir.string(), "index");
    store.open("test-model");

    store.add("a", "alpha", {1.0f, 0.0f, 0.0f, 0.0f});
    store.add("b", "beta", {0.0f, 1.0f, 0.0f, 0.0f});
    store.remove("a");

    auto results = store.search({1.0f, 0.0f, 0.0f, 0.0f}, 5);

    for (const auto &r : results)
    {
        CHECK(r.id != "a");
    }

    fs::remove_all(dir);
}

TEST_CASE("a dimension mismatch is rejected rather than corrupting the index")
{
    auto dir = makeStoreDir();
    VectorStore store(dir.string(), "index");
    store.open("test-model");

    store.add("a", "alpha", {1.0f, 0.0f, 0.0f, 0.0f});
    store.add("bad", "wrong-dim", {1.0f, 0.0f}); // different dimension, ignored

    auto results = store.search({1.0f, 0.0f, 0.0f, 0.0f}, 5);

    for (const auto &r : results)
    {
        CHECK(r.id != "bad");
    }

    fs::remove_all(dir);
}
