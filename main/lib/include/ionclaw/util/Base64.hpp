#pragma once

#include <cstddef>
#include <string>

namespace ionclaw
{
namespace util
{

class Base64
{
public:
    /** Encode binary data to base64. */
    static std::string encode(const unsigned char *data, size_t len);

    /** Encode a string (binary-safe). */
    static std::string encode(const std::string &data);

    /** Decode base64 to binary. Skips whitespace and invalid chars. */
    static std::string decode(const std::string &encoded);

    /** Read file at path and return contents as base64. Returns empty string on error. */
    static std::string encodeFromFile(const std::string &path);

private:
    // encoding and decoding lookup tables
    static const char ENCODE_CHARS[];
    static const unsigned char DECODE_TABLE[];
};

} // namespace util
} // namespace ionclaw
