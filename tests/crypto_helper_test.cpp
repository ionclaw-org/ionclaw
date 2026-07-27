#include "doctest/doctest.h"

#include <set>
#include <string>

#include "ionclaw/util/Base64.hpp"
#include "ionclaw/util/RandomHelper.hpp"

using ionclaw::util::Base64;
using ionclaw::util::RandomHelper;

TEST_CASE("base64 round-trips arbitrary bytes including embedded nulls")
{
    std::string data("a\0b\xff\x10zzz", 8);
    CHECK(Base64::decode(Base64::encode(data)) == data);
}

TEST_CASE("base64 handles the three padding cases")
{
    CHECK(Base64::encode(std::string("")) == "");
    CHECK(Base64::decode(Base64::encode(std::string("f"))) == "f");
    CHECK(Base64::decode(Base64::encode(std::string("fo"))) == "fo");
    CHECK(Base64::decode(Base64::encode(std::string("foo"))) == "foo");
    CHECK(Base64::encode(std::string("foobar")) == "Zm9vYmFy");
}

TEST_CASE("base64 decode ignores stray whitespace and newlines")
{
    CHECK(Base64::decode("Zm9v\nYmFy") == "foobar");
    CHECK(Base64::decode("Zm9v YmFy") == "foobar");
}

TEST_CASE("secureHex returns the requested byte count as hex and varies between calls")
{
    auto a = RandomHelper::secureHex(16);
    auto b = RandomHelper::secureHex(16);

    CHECK(a.size() == 32);
    CHECK(b.size() == 32);
    CHECK(a != b);

    for (char c : a)
    {
        bool isLowerHex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        CHECK(isLowerHex);
    }
}

TEST_CASE("secureUint64 does not repeat across a small sample")
{
    std::set<uint64_t> seen;

    for (int i = 0; i < 128; ++i)
    {
        seen.insert(RandomHelper::secureUint64());
    }

    CHECK(seen.size() == 128);
}

TEST_CASE("constantTimeEquals matches only equal strings and rejects length mismatch")
{
    CHECK(RandomHelper::constantTimeEquals("secret-token", "secret-token"));
    CHECK_FALSE(RandomHelper::constantTimeEquals("secret-token", "secret-tokeX"));
    CHECK_FALSE(RandomHelper::constantTimeEquals("secret", "secret-token"));
    CHECK(RandomHelper::constantTimeEquals("", ""));
}
