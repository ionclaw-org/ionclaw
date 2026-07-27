#pragma once

#ifdef IONCLAW_HAS_LLAMA_CPP

#include <mutex>

namespace ionclaw
{
namespace provider
{

class LlamaBackend
{
public:
    static void acquire();
    static void release();

private:
    static std::mutex mutex;
    static int refCount;
};

} // namespace provider
} // namespace ionclaw

#endif // IONCLAW_HAS_LLAMA_CPP
