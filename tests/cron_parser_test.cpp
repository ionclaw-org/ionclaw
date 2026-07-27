#include "doctest/doctest.h"

#include "ionclaw/cron/CronParser.hpp"

using ionclaw::cron::CronParser;

TEST_CASE("isValidExpression accepts well-formed 5-field expressions")
{
    CHECK(CronParser::isValidExpression("* * * * *"));
    CHECK(CronParser::isValidExpression("0 9 * * 1-5"));
    CHECK(CronParser::isValidExpression("*/15 * * * *"));
    CHECK(CronParser::isValidExpression("0,30 0-12 1 1 0"));
}

TEST_CASE("isValidExpression rejects malformed expressions")
{
    CHECK_FALSE(CronParser::isValidExpression(""));
    CHECK_FALSE(CronParser::isValidExpression("* * * *"));
    CHECK_FALSE(CronParser::isValidExpression("* * * * * *"));
    CHECK_FALSE(CronParser::isValidExpression("99 * * * *"));
    CHECK_FALSE(CronParser::isValidExpression("* 25 * * *"));
}

TEST_CASE("nextRun returns a future epoch for a valid expression")
{
    auto next = CronParser::nextRun("* * * * *");
    CHECK(next > 0);
}

TEST_CASE("isValidTimezone accepts real zones and rejects empty or traversal input")
{
    // portable across platforms: strict where the zoneinfo database exists, shape-based where it does not
    CHECK(CronParser::isValidTimezone("UTC"));
    CHECK(CronParser::isValidTimezone("America/Sao_Paulo"));
    CHECK_FALSE(CronParser::isValidTimezone(""));
    CHECK_FALSE(CronParser::isValidTimezone("../../etc/passwd"));
}
