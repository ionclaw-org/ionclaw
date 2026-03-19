#include "ionclaw/util/Base64.hpp"

#include <fstream>

#include "spdlog/spdlog.h"

namespace ionclaw
{
namespace util
{

// encoding lookup table
const char Base64::ENCODE_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// decoding lookup table (64 = invalid)
const unsigned char Base64::DECODE_TABLE[256] = {
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    62,
    64,
    64,
    64,
    63,
    52,
    53,
    54,
    55,
    56,
    57,
    58,
    59,
    60,
    61,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    0,
    1,
    2,
    3,
    4,
    5,
    6,
    7,
    8,
    9,
    10,
    11,
    12,
    13,
    14,
    15,
    16,
    17,
    18,
    19,
    20,
    21,
    22,
    23,
    24,
    25,
    64,
    64,
    64,
    64,
    64,
    64,
    26,
    27,
    28,
    29,
    30,
    31,
    32,
    33,
    34,
    35,
    36,
    37,
    38,
    39,
    40,
    41,
    42,
    43,
    44,
    45,
    46,
    47,
    48,
    49,
    50,
    51,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
    64,
};

// encode binary data to base64
std::string Base64::encode(const unsigned char *data, size_t len)
{
    std::string out;
    out.reserve(((len + 2) / 3) * 4);

    for (size_t i = 0; i < len; i += 3)
    {
        unsigned int n = static_cast<unsigned int>(data[i]) << 16;

        if (i + 1 < len)
        {
            n |= static_cast<unsigned int>(data[i + 1]) << 8;
        }

        if (i + 2 < len)
        {
            n |= static_cast<unsigned int>(data[i + 2]);
        }

        out.push_back(ENCODE_CHARS[(n >> 18) & 63]);
        out.push_back(ENCODE_CHARS[(n >> 12) & 63]);
        out.push_back(i + 1 < len ? ENCODE_CHARS[(n >> 6) & 63] : '=');
        out.push_back(i + 2 < len ? ENCODE_CHARS[n & 63] : '=');
    }

    return out;
}

std::string Base64::encode(const std::string &data)
{
    if (data.empty())
    {
        return "";
    }

    return encode(reinterpret_cast<const unsigned char *>(data.data()), data.size());
}

std::string Base64::decode(const std::string &encoded)
{
    std::string decoded;
    decoded.reserve(encoded.size() * 3 / 4);

    unsigned int buffer = 0;
    int bits = 0;

    for (unsigned char c : encoded)
    {
        // skip padding and whitespace
        if (c == '=' || c == '\n' || c == '\r' || c == ' ')
        {
            continue;
        }

        unsigned char val = DECODE_TABLE[c];

        // skip invalid characters
        if (val == 64)
        {
            continue;
        }

        buffer = (buffer << 6) | val;
        bits += 6;

        if (bits >= 8)
        {
            bits -= 8;
            decoded.push_back(static_cast<char>((buffer >> bits) & 0xFF));
        }
    }

    return decoded;
}

std::string Base64::encodeFromFile(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);

    if (!f.is_open())
    {
        spdlog::error("[Base64] Failed to open file for encoding: {}", path);
        return "";
    }

    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();

    if (data.empty())
    {
        return "";
    }

    return encode(reinterpret_cast<const unsigned char *>(data.data()), data.size());
}

} // namespace util
} // namespace ionclaw
