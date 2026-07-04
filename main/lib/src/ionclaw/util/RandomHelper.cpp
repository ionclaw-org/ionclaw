#include "ionclaw/util/RandomHelper.hpp"

#include <stdexcept>
#include <vector>

#include <openssl/crypto.h>
#include <openssl/rand.h>

namespace ionclaw
{
namespace util
{

std::string RandomHelper::secureHex(std::size_t byteCount)
{
    std::vector<unsigned char> buffer(byteCount);

    if (RAND_bytes(buffer.data(), static_cast<int>(byteCount)) != 1)
    {
        throw std::runtime_error("[RandomHelper] Failed to obtain secure random bytes");
    }

    static const char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(byteCount * 2);

    for (auto byte : buffer)
    {
        result += hex[byte >> 4];
        result += hex[byte & 0x0F];
    }

    return result;
}

uint64_t RandomHelper::secureUint64()
{
    uint64_t value = 0;

    if (RAND_bytes(reinterpret_cast<unsigned char *>(&value), sizeof(value)) != 1)
    {
        throw std::runtime_error("[RandomHelper] Failed to obtain secure random bytes");
    }

    return value;
}

bool RandomHelper::constantTimeEquals(const std::string &a, const std::string &b)
{
    // reject differing lengths first so the comparison below always operates on equal-length buffers
    if (a.size() != b.size())
    {
        return false;
    }

    return CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}

} // namespace util
} // namespace ionclaw
