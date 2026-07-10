#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "ionclaw/embedding/VectorStore.hpp"

namespace ionclaw
{
namespace config
{
struct Config;
}

namespace agent
{

struct SemanticMemoryResult
{
    std::string file;
    std::string text;
    float score = 0.0f;
};

class SemanticMemory
{
public:
    SemanticMemory(const std::string &workspacePath, const ionclaw::config::Config &config);

    bool available() const;
    std::vector<SemanticMemoryResult> search(const std::string &query, size_t topK);

private:
    std::string memoryDir;
    std::string indexDir;
    const ionclaw::config::Config &config;
    ionclaw::embedding::VectorStore store;

    std::string providerName() const;
    void sync();
};

} // namespace agent
} // namespace ionclaw
