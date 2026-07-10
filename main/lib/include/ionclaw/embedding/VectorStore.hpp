#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace ionclaw
{
namespace embedding
{

struct VectorSearchResult
{
    std::string id;
    std::string text;
    float score = 0.0f;
    nlohmann::json metadata;
};

class VectorStore
{
public:
    VectorStore(std::string directory, std::string name);
    ~VectorStore();

    VectorStore(const VectorStore &) = delete;
    VectorStore &operator=(const VectorStore &) = delete;

    void open(const std::string &embeddingModel);
    bool isOpen() const;

    void add(const std::string &id, const std::string &text, const std::vector<float> &vector, const nlohmann::json &metadata = nlohmann::json::object());
    void remove(const std::string &id);
    std::vector<VectorSearchResult> search(const std::vector<float> &queryVector, size_t topK) const;

    size_t count() const;
    std::string embeddingModel() const;
    void save() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    static std::vector<float> normalize(const std::vector<float> &vector);
};

} // namespace embedding
} // namespace ionclaw
