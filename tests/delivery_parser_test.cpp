#include "doctest/doctest.h"

#include "ionclaw/channel/DeliveryParser.hpp"

using namespace ionclaw::channel;

TEST_CASE("parse returns a single text part for a plain reply")
{
    auto parts = DeliveryParser::parse("hello there");

    REQUIRE(parts.size() == 1);
    CHECK(parts[0].text == "hello there");
    CHECK(parts[0].mediaPath.empty());
}

TEST_CASE("parse splits on the break marker")
{
    auto parts = DeliveryParser::parse("first message[[break]]second message");

    REQUIRE(parts.size() == 2);
    CHECK(parts[0].text == "first message");
    CHECK(parts[1].text == "second message");
}

TEST_CASE("parse drops empty segments produced by breaks")
{
    auto parts = DeliveryParser::parse("only[[break]]   [[break]]tail");

    REQUIRE(parts.size() == 2);
    CHECK(parts[0].text == "only");
    CHECK(parts[1].text == "tail");
}

TEST_CASE("parse extracts a media marker with its caption")
{
    auto parts = DeliveryParser::parse("here is the chart [[image:public/media/chart.png]]");

    REQUIRE(parts.size() == 1);
    CHECK(parts[0].mediaKind == "image");
    CHECK(parts[0].mediaPath == "public/media/chart.png");
    CHECK(parts[0].text == "here is the chart");
}

TEST_CASE("parse maps the marker aliases to canonical kinds")
{
    CHECK(DeliveryParser::parse("[[photo:a.png]]")[0].mediaKind == "image");
    CHECK(DeliveryParser::parse("[[voice:a.ogg]]")[0].mediaKind == "audio");
    CHECK(DeliveryParser::parse("[[file:a.pdf]]")[0].mediaKind == "document");
    CHECK(DeliveryParser::parse("[[video:a.mp4]]")[0].mediaKind == "video");
}

TEST_CASE("parse infers the kind from the extension for a generic media marker")
{
    CHECK(DeliveryParser::parse("[[media:a.png]]")[0].mediaKind == "image");
    CHECK(DeliveryParser::parse("[[media:a.mp3]]")[0].mediaKind == "audio");
    CHECK(DeliveryParser::parse("[[media:a.mp4]]")[0].mediaKind == "video");
    CHECK(DeliveryParser::parse("[[media:a.pdf]]")[0].mediaKind == "document");
    CHECK(DeliveryParser::parse("[[media:noext]]")[0].mediaKind == "document");
}

TEST_CASE("parse keeps the caption only on the first media part of a segment")
{
    auto parts = DeliveryParser::parse("two files [[image:a.png]] [[document:b.pdf]]");

    REQUIRE(parts.size() == 2);
    CHECK(parts[0].mediaPath == "a.png");
    CHECK(parts[0].text == "two files");
    CHECK(parts[1].mediaPath == "b.pdf");
    CHECK(parts[1].text.empty());
}

TEST_CASE("parse leaves unknown or malformed markers as literal text")
{
    auto parts = DeliveryParser::parse("keep [[bold]] this and [[image:]] empty");

    REQUIRE(parts.size() == 1);
    CHECK(parts[0].mediaPath.empty());
    CHECK(parts[0].text == "keep [[bold]] this and [[image:]] empty");
}

TEST_CASE("parse returns nothing for an empty reply")
{
    CHECK(DeliveryParser::parse("").empty());
    CHECK(DeliveryParser::parse("   ").empty());
}
