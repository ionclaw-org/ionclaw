#ifdef IONCLAW_HAS_LLAMA_CPP

#include "ionclaw/provider/LlamaBackend.hpp"

#include "llama.h"

namespace ionclaw
{
namespace provider
{

std::mutex LlamaBackend::mutex;
int LlamaBackend::refCount = 0;

void LlamaBackend::acquire()
{
    std::lock_guard<std::mutex> lock(mutex);

    if (refCount == 0)
    {
        llama_backend_init();
    }

    ++refCount;
}

void LlamaBackend::release()
{
    std::lock_guard<std::mutex> lock(mutex);

    if (refCount <= 0)
    {
        return;
    }

    if (--refCount == 0)
    {
        llama_backend_free();
    }
}

} // namespace provider
} // namespace ionclaw

#endif // IONCLAW_HAS_LLAMA_CPP
