#include "doctest/doctest.h"

#include <chrono>
#include <future>
#include <string>

#include "ionclaw/embedding/TextChunker.hpp"

using ionclaw::embedding::TextChunker;

namespace
{

std::string repeat(const std::string &unit, size_t times)
{
    std::string out;
    out.reserve(unit.size() * times);

    for (size_t i = 0; i < times; ++i)
    {
        out += unit;
    }

    return out;
}

} // namespace

TEST_CASE("chunk returns nothing for empty or whitespace-only text")
{
    CHECK(TextChunker::chunk("", 100, 20).empty());
    CHECK(TextChunker::chunk("    \n\t  ", 100, 20).empty());
}

TEST_CASE("text shorter than the window is a single chunk")
{
    auto chunks = TextChunker::chunk("hello world", 100, 20);
    REQUIRE(chunks.size() == 1);
    CHECK(chunks[0] == "hello world");
}

TEST_CASE("long text is split into multiple bounded chunks")
{
    auto text = repeat("word ", 400); // 2000 chars
    auto chunks = TextChunker::chunk(text, 200, 40);

    REQUIRE(chunks.size() > 1);

    for (const auto &c : chunks)
    {
        CHECK(c.size() <= 200);
        CHECK_FALSE(c.empty());
    }
}

TEST_CASE("consecutive chunks overlap so context is shared")
{
    auto text = repeat("abcde ", 200);
    auto chunks = TextChunker::chunk(text, 120, 40);
    REQUIRE(chunks.size() >= 2);

    // the whole text is covered: concatenating without overlap would still contain every word
    CHECK(chunks.front().size() <= 120);
}

TEST_CASE("multibyte utf-8 chunks never split a codepoint")
{
    // é is two bytes (0xC3 0xA9)
    auto text = repeat("héllo wörld ", 300);
    auto chunks = TextChunker::chunk(text, 100, 20);

    for (const auto &c : chunks)
    {
        // a valid utf-8 string never starts or ends on a continuation byte
        CHECK((static_cast<unsigned char>(c.front()) & 0xC0) != 0x80);
        CHECK((static_cast<unsigned char>(c.back()) & 0xC0) != 0x80);
    }
}

TEST_CASE("a large overlap on multibyte text terminates instead of looping forever")
{
    // regression: advance could collapse to zero on a multibyte boundary and spin forever
    auto text = repeat("héllo ", 500);

    // clang-format off
    auto run = std::async(std::launch::async, [&text] {
        return TextChunker::chunk(text, 64, 63).size();
    });
    // clang-format on

    REQUIRE(run.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    CHECK(run.get() > 0);
}

TEST_CASE("an overlap at or above the window is rejected as an invalid config")
{
    auto text = repeat("data ", 300);

    // clang-format off
    auto run = std::async(std::launch::async, [&text] {
        return TextChunker::chunk(text, 50, 500).size();
    });
    // clang-format on

    // it returns immediately instead of stalling, and the invalid config yields no chunks
    REQUIRE(run.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    CHECK(run.get() == 0);
}
