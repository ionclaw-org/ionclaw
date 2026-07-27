#include "doctest/doctest.h"

#include <ctime>

#include "ionclaw/util/TimeHelper.hpp"

using ionclaw::util::TimeHelper;

namespace
{

std::tm makeTm(int year, int month, int day, int hour, int minute, int second)
{
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;
    tm.tm_isdst = 0;
    return tm;
}

} // namespace

TEST_CASE("timegmUtc interprets the broken-down time as utc, not local")
{
    // 2001-09-09T01:46:40Z is exactly 1000000000 epoch seconds
    auto tm = makeTm(2001, 9, 9, 1, 46, 40);
    CHECK(TimeHelper::timegmUtc(tm) == static_cast<std::time_t>(1000000000));
}

TEST_CASE("timegmUtc round-trips the unix epoch")
{
    auto tm = makeTm(1970, 1, 1, 0, 0, 0);
    CHECK(TimeHelper::timegmUtc(tm) == static_cast<std::time_t>(0));
}

TEST_CASE("timegmUtc is independent of the field order of the day")
{
    auto midday = makeTm(2026, 3, 5, 12, 0, 0);
    auto midnight = makeTm(2026, 3, 5, 0, 0, 0);
    CHECK(TimeHelper::timegmUtc(midday) - TimeHelper::timegmUtc(midnight) == 12 * 3600);
}

TEST_CASE("now produces a fixed-width iso-8601 utc timestamp")
{
    auto ts = TimeHelper::now();
    // e.g. 2026-07-27T00:00:00Z or with milliseconds, always ending in Z
    CHECK(ts.size() >= 20);
    CHECK(ts.back() == 'Z');
    CHECK(ts[4] == '-');
    CHECK(ts[10] == 'T');
}

TEST_CASE("diffSeconds returns the signed gap and a sentinel on malformed input")
{
    auto a = "2026-01-01T00:00:00Z";
    auto b = "2026-01-01T00:01:00Z";
    CHECK(TimeHelper::diffSeconds(a, b) == 60);
    CHECK(TimeHelper::diffSeconds(b, a) == -60);
    // a malformed timestamp returns 0 rather than a decades-off diff against 1970
    CHECK(TimeHelper::diffSeconds("not-a-date", b) == 0);
}
