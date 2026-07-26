#include "ionclaw/agent/SemanticMemory.hpp"

#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <sstream>

#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

#include "ionclaw/config/Config.hpp"
#include "ionclaw/embedding/EmbeddingProvider.hpp"
#include "ionclaw/embedding/EmbeddingProviderRegistry.hpp"
#include "ionclaw/embedding/TextChunker.hpp"

namespace ionclaw
{
namespace agent
{

namespace fs = std::filesystem;

namespace
{
constexpr size_t CHUNK_MAX_CHARS = 1000;
constexpr size_t CHUNK_OVERLAP_CHARS = 150;

std::string readFile(const fs::path &path)
{
    std::ifstream file(path, std::ios::binary);

    if (!file.is_open())
    {
        return "";
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string contentHash(const std::string &content)
{
    std::ostringstream out;
    out << std::hex << std::hash<std::string>{}(content);
    return out.str();
}
} // namespace

SemanticMemory::SemanticMemory(const std::string &workspacePath, const ionclaw::config::Config &config)
    : memoryDir(workspacePath + "/memory")
    , indexDir(workspacePath + "/memory/.index")
    , config(config)
    , store(indexDir, "memory")
{
}

std::mutex &SemanticMemory::directoryMutex(const std::string &directory)
{
    static std::mutex mapMutex;
    static std::map<std::string, std::unique_ptr<std::mutex>> mutexes;

    std::lock_guard<std::mutex> lock(mapMutex);
    auto &entry = mutexes[directory];

    if (!entry)
    {
        entry = std::make_unique<std::mutex>();
    }

    return *entry;
}

std::string SemanticMemory::providerName() const
{
    auto slashPos = config.embeddings.model.find('/');
    return slashPos != std::string::npos ? config.embeddings.model.substr(0, slashPos) : config.embeddings.model;
}

bool SemanticMemory::available() const
{
    if (config.embeddings.model.empty())
    {
        return false;
    }

    return ionclaw::embedding::EmbeddingProviderRegistry::instance().get(providerName()) != nullptr;
}

void SemanticMemory::sync()
{
    auto *provider = ionclaw::embedding::EmbeddingProviderRegistry::instance().get(providerName());

    if (!provider)
    {
        return;
    }

    const auto &model = config.embeddings.model;
    store.open(model);

    ionclaw::embedding::EmbeddingContext ctx;
    ctx.model = model;
    ctx.providerName = providerName();
    ctx.config = &config;

    // the sidecar tracks the content hash and chunk count per file so only changed files are re-embedded
    auto sidecarPath = (fs::path(indexDir) / "files.json").string();
    nlohmann::json sidecar = nlohmann::json::object();

    {
        std::ifstream in(sidecarPath);

        if (in.is_open())
        {
            sidecar = nlohmann::json::parse(in, nullptr, false);
        }
    }

    // a model change invalidates every stored vector, so the tracking restarts from scratch
    if (!sidecar.is_object() || sidecar.value("model", "") != model)
    {
        sidecar = nlohmann::json::object();
        sidecar["model"] = model;
    }

    auto &files = sidecar["files"];

    if (!files.is_object())
    {
        files = nlohmann::json::object();
    }

    // the index and sidecar are only rewritten when a file was actually added, changed or removed
    bool changed = false;

    std::set<std::string> present;
    std::error_code ec;

    for (const auto &entry : fs::directory_iterator(memoryDir, ec))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".md")
        {
            continue;
        }

        auto path = entry.path().string();
        present.insert(path);

        auto content = readFile(entry.path());
        auto hash = contentHash(content);

        if (files.contains(path) && files[path].value("hash", "") == hash)
        {
            continue;
        }

        // drop the previous chunks of this file before indexing the new content
        auto oldChunks = files.contains(path) ? files[path].value("chunks", size_t{0}) : size_t{0};

        for (size_t i = 0; i < oldChunks; ++i)
        {
            store.remove(path + ":" + std::to_string(i));
        }

        auto chunks = ionclaw::embedding::TextChunker::chunk(content, CHUNK_MAX_CHARS, CHUNK_OVERLAP_CHARS);

        if (chunks.empty())
        {
            if (files.contains(path))
            {
                files.erase(path);
                changed = true;
            }

            continue;
        }

        auto vectors = provider->embed(chunks, ctx);

        if (vectors.size() != chunks.size())
        {
            spdlog::error("[SemanticMemory] embedding failed for '{}', it will be retried on the next search", path);
            continue;
        }

        auto fileName = entry.path().filename().string();

        for (size_t i = 0; i < chunks.size(); ++i)
        {
            store.add(path + ":" + std::to_string(i), chunks[i], vectors[i], {{"file", fileName}});
        }

        files[path] = {{"hash", hash}, {"chunks", chunks.size()}};
        changed = true;
    }

    // forget files that were removed from the memory directory
    std::vector<std::string> gone;

    for (const auto &item : files.items())
    {
        if (present.find(item.key()) == present.end())
        {
            gone.push_back(item.key());
        }
    }

    for (const auto &path : gone)
    {
        auto oldChunks = files[path].value("chunks", size_t{0});

        for (size_t i = 0; i < oldChunks; ++i)
        {
            store.remove(path + ":" + std::to_string(i));
        }

        files.erase(path);
        changed = true;
    }

    if (!changed)
    {
        return;
    }

    store.save();

    fs::create_directories(indexDir, ec);

    // write the sidecar to a temporary then rename so it stays consistent with the index on a crash
    auto sidecarTmp = sidecarPath + ".tmp";
    std::ofstream out(sidecarTmp, std::ios::trunc);

    if (out.is_open())
    {
        out << sidecar.dump();
        out.close();
        fs::rename(sidecarTmp, sidecarPath, ec);
    }
}

std::vector<SemanticMemoryResult> SemanticMemory::search(const std::string &query, size_t topK)
{
    if (!available() || query.empty() || topK == 0)
    {
        return {};
    }

    // serialize instances that share this on-disk index so concurrent searches never interleave their index writes
    std::lock_guard<std::mutex> dirLock(directoryMutex(indexDir));

    sync();

    auto *provider = ionclaw::embedding::EmbeddingProviderRegistry::instance().get(providerName());

    ionclaw::embedding::EmbeddingContext ctx;
    ctx.model = config.embeddings.model;
    ctx.providerName = providerName();
    ctx.config = &config;

    auto queryVectors = provider->embed({query}, ctx);

    if (queryVectors.empty())
    {
        return {};
    }

    auto matches = store.search(queryVectors.front(), topK);

    std::vector<SemanticMemoryResult> results;
    results.reserve(matches.size());

    for (const auto &match : matches)
    {
        SemanticMemoryResult result;
        result.file = match.metadata.value("file", "");
        result.text = match.text;
        result.score = match.score;

        results.push_back(std::move(result));
    }

    return results;
}

} // namespace agent
} // namespace ionclaw
