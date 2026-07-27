#include "ionclaw/embedding/VectorStore.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>

#include "hnswlib/hnswlib.h"
#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace embedding
{

namespace fs = std::filesystem;

namespace
{
constexpr size_t INITIAL_CAPACITY = 1024;
constexpr size_t HNSW_M = 16;
constexpr size_t HNSW_EF_CONSTRUCTION = 200;
constexpr size_t HNSW_MIN_EF_SEARCH = 64;
} // namespace

struct VectorStore::Impl
{
    struct Entry
    {
        std::string id;
        std::string text;
        nlohmann::json metadata;
    };

    std::string directory;
    std::string name;
    std::string model;
    size_t dim = 0;

    std::unique_ptr<hnswlib::InnerProductSpace> space;
    std::unique_ptr<hnswlib::HierarchicalNSW<float>> index;

    hnswlib::labeltype nextLabel = 0;
    std::unordered_map<std::string, hnswlib::labeltype> idToLabel;
    std::unordered_map<hnswlib::labeltype, Entry> entries;

    mutable std::mutex mutex;

    std::string indexPath() const
    {
        return (fs::path(directory) / (name + ".hnsw")).string();
    }

    std::string metaPath() const
    {
        return (fs::path(directory) / (name + ".json")).string();
    }
};

VectorStore::VectorStore(std::string directory, std::string name)
    : impl(std::make_unique<Impl>())
{
    impl->directory = std::move(directory);
    impl->name = std::move(name);
}

VectorStore::~VectorStore() = default;

std::vector<float> VectorStore::normalize(const std::vector<float> &vector)
{
    double sum = 0.0;

    for (float value : vector)
    {
        sum += static_cast<double>(value) * static_cast<double>(value);
    }

    auto norm = std::sqrt(sum);

    if (norm == 0.0)
    {
        return vector;
    }

    std::vector<float> normalized(vector.size());

    for (size_t i = 0; i < vector.size(); ++i)
    {
        normalized[i] = static_cast<float>(vector[i] / norm);
    }

    return normalized;
}

void VectorStore::open(const std::string &embeddingModel)
{
    std::lock_guard<std::mutex> lock(impl->mutex);

    if (impl->index && impl->model == embeddingModel)
    {
        return;
    }

    impl->model = embeddingModel;
    impl->dim = 0;
    impl->space.reset();
    impl->index.reset();
    impl->idToLabel.clear();
    impl->entries.clear();
    impl->nextLabel = 0;

    // a persisted store is reused only when its model matches, otherwise the first add rebuilds it from the current dimension
    if (!fs::exists(impl->metaPath()) || !fs::exists(impl->indexPath()))
    {
        return;
    }

    std::ifstream in(impl->metaPath());
    auto meta = nlohmann::json::parse(in, nullptr, false);

    if (meta.is_discarded() || meta.value("model", "") != embeddingModel)
    {
        return;
    }

    impl->dim = meta.value("dimension", size_t{0});

    if (impl->dim == 0)
    {
        return;
    }

    // a corrupt or partially-written index must not break search forever, so on any load failure the store resets to empty and the next add rebuilds it
    try
    {
        impl->space = std::make_unique<hnswlib::InnerProductSpace>(impl->dim);
        auto capacity = std::max<size_t>(INITIAL_CAPACITY, meta.value("next_label", size_t{0}));
        impl->index = std::make_unique<hnswlib::HierarchicalNSW<float>>(impl->space.get(), impl->indexPath(), false, capacity);
        impl->nextLabel = meta.value("next_label", size_t{0});

        // iterate the named meta member rather than a value() temporary, whose json would dangle during the loop
        if (meta.contains("entries") && meta["entries"].is_object())
        {
            for (const auto &item : meta["entries"].items())
            {
                auto label = static_cast<hnswlib::labeltype>(std::stoull(item.key()));
                Impl::Entry entry;
                entry.id = item.value().value("id", "");
                entry.text = item.value().value("text", "");
                entry.metadata = item.value().value("metadata", nlohmann::json::object());

                impl->entries[label] = entry;
                impl->idToLabel[entry.id] = label;
            }
        }
    }
    catch (const std::exception &e)
    {
        spdlog::error("[VectorStore] Failed to load index '{}', rebuilding: {}", impl->name, e.what());
        impl->space.reset();
        impl->index.reset();
        impl->idToLabel.clear();
        impl->entries.clear();
        impl->nextLabel = 0;
        impl->dim = 0;
        return;
    }

    spdlog::info("[VectorStore] Opened '{}' with {} entries (model={}, dim={})", impl->name, impl->entries.size(), embeddingModel, impl->dim);
}

bool VectorStore::isOpen() const
{
    std::lock_guard<std::mutex> lock(impl->mutex);
    return !impl->model.empty();
}

void VectorStore::add(const std::string &id, const std::string &text, const std::vector<float> &vector, const nlohmann::json &metadata)
{
    std::lock_guard<std::mutex> lock(impl->mutex);

    if (impl->model.empty())
    {
        spdlog::error("[VectorStore] Add called before open");
        return;
    }

    if (vector.empty())
    {
        return;
    }

    // the first vector fixes the dimension and builds the index, since the embedding model determines it
    if (!impl->index)
    {
        impl->dim = vector.size();
        impl->space = std::make_unique<hnswlib::InnerProductSpace>(impl->dim);
        impl->index = std::make_unique<hnswlib::HierarchicalNSW<float>>(impl->space.get(), INITIAL_CAPACITY, HNSW_M, HNSW_EF_CONSTRUCTION);
    }

    if (vector.size() != impl->dim)
    {
        spdlog::error("[VectorStore] Vector dimension {} does not match store dimension {}", vector.size(), impl->dim);
        return;
    }

    // a re-added id replaces its previous vector, so the old label is dropped from search and bookkeeping
    auto existing = impl->idToLabel.find(id);

    if (existing != impl->idToLabel.end())
    {
        impl->index->markDelete(existing->second);
        impl->entries.erase(existing->second);
        impl->idToLabel.erase(existing);
    }

    if (impl->nextLabel >= impl->index->getMaxElements())
    {
        impl->index->resizeIndex(impl->index->getMaxElements() * 2);
    }

    auto normalized = normalize(vector);
    auto label = impl->nextLabel++;

    impl->index->addPoint(normalized.data(), label);

    Impl::Entry entry;
    entry.id = id;
    entry.text = text;
    entry.metadata = metadata;

    impl->entries[label] = std::move(entry);
    impl->idToLabel[id] = label;
}

void VectorStore::remove(const std::string &id)
{
    std::lock_guard<std::mutex> lock(impl->mutex);

    if (!impl->index)
    {
        return;
    }

    auto existing = impl->idToLabel.find(id);

    if (existing == impl->idToLabel.end())
    {
        return;
    }

    impl->index->markDelete(existing->second);
    impl->entries.erase(existing->second);
    impl->idToLabel.erase(existing);
}

std::vector<VectorSearchResult> VectorStore::search(const std::vector<float> &queryVector, size_t topK) const
{
    std::lock_guard<std::mutex> lock(impl->mutex);

    if (!impl->index || topK == 0 || queryVector.size() != impl->dim || impl->entries.empty())
    {
        return {};
    }

    auto k = std::min(topK, impl->entries.size());
    impl->index->setEf(std::max(k, HNSW_MIN_EF_SEARCH));

    auto normalized = normalize(queryVector);
    auto queue = impl->index->searchKnn(normalized.data(), k);

    // the queue yields the farthest match first, so results are reversed into best-first order
    std::vector<VectorSearchResult> results;
    results.reserve(queue.size());

    while (!queue.empty())
    {
        auto [distance, label] = queue.top();
        queue.pop();

        auto it = impl->entries.find(label);

        if (it == impl->entries.end())
        {
            continue;
        }

        VectorSearchResult result;
        result.id = it->second.id;
        result.text = it->second.text;
        result.metadata = it->second.metadata;
        result.score = 1.0f - distance;

        results.push_back(std::move(result));
    }

    std::reverse(results.begin(), results.end());
    return results;
}

size_t VectorStore::count() const
{
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->entries.size();
}

std::string VectorStore::embeddingModel() const
{
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->model;
}

void VectorStore::save() const
{
    std::lock_guard<std::mutex> lock(impl->mutex);

    if (!impl->index)
    {
        return;
    }

    std::error_code ec;
    fs::create_directories(impl->directory, ec);

    if (ec)
    {
        spdlog::error("[VectorStore] Failed to create directory '{}': {}", impl->directory, ec.message());
        return;
    }

    nlohmann::json entries = nlohmann::json::object();

    for (const auto &[label, entry] : impl->entries)
    {
        entries[std::to_string(label)] = {{"id", entry.id}, {"text", entry.text}, {"metadata", entry.metadata}};
    }

    nlohmann::json meta;
    meta["model"] = impl->model;
    meta["dimension"] = impl->dim;
    meta["next_label"] = impl->nextLabel;
    meta["entries"] = std::move(entries);

    // write both files to temporaries then rename so a crash never leaves a truncated index or metadata as the live file
    auto indexTmp = impl->indexPath() + ".tmp";
    auto metaTmp = impl->metaPath() + ".tmp";

    impl->index->saveIndex(indexTmp);

    std::ofstream out(metaTmp, std::ios::trunc);

    if (!out.is_open())
    {
        spdlog::error("[VectorStore] Failed to write metadata '{}'", metaTmp);
        return;
    }

    out << meta.dump();
    out.close();

    if (!out)
    {
        spdlog::error("[VectorStore] Failed to flush metadata '{}'", metaTmp);
        return;
    }

    // commit the index first, and only publish the matching metadata once the index is live so the two never diverge
    fs::rename(indexTmp, impl->indexPath(), ec);

    if (ec)
    {
        spdlog::error("[VectorStore] Failed to commit index '{}': {}", impl->indexPath(), ec.message());
        return;
    }

    fs::rename(metaTmp, impl->metaPath(), ec);

    if (ec)
    {
        spdlog::error("[VectorStore] Committed index but failed to commit metadata '{}': {}", impl->metaPath(), ec.message());
    }
}

} // namespace embedding
} // namespace ionclaw
