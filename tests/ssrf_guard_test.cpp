#include "doctest/doctest.h"

#include "ionclaw/util/SsrfGuard.hpp"

using ionclaw::util::SsrfGuard;

TEST_CASE("private and loopback ipv4 literals are rejected")
{
    CHECK_THROWS_AS(SsrfGuard::validateUrl("http://127.0.0.1/"), std::runtime_error);
    CHECK_THROWS_AS(SsrfGuard::validateUrl("http://10.0.0.5/"), std::runtime_error);
    CHECK_THROWS_AS(SsrfGuard::validateUrl("http://192.168.1.1/"), std::runtime_error);
    CHECK_THROWS_AS(SsrfGuard::validateUrl("http://172.16.0.1/"), std::runtime_error);
    CHECK_THROWS_AS(SsrfGuard::validateUrl("http://169.254.0.1/"), std::runtime_error);
    CHECK_THROWS_AS(SsrfGuard::validateUrl("http://100.64.0.1/"), std::runtime_error);
    CHECK_THROWS_AS(SsrfGuard::validateUrl("http://0.0.0.0/"), std::runtime_error);
}

TEST_CASE("the decimal encoding of loopback is rejected")
{
    // 2130706433 is 127.0.0.1 as a single 32-bit integer, which the guard resolves and blocks
    CHECK_THROWS_AS(SsrfGuard::validateUrl("http://2130706433/"), std::runtime_error);
}

TEST_CASE("ipv6 loopback and mapped private addresses are rejected")
{
    CHECK_THROWS_AS(SsrfGuard::validateUrl("http://[::1]/"), std::runtime_error);
    CHECK_THROWS_AS(SsrfGuard::validateUrl("http://[::ffff:10.0.0.1]/"), std::runtime_error);
}

TEST_CASE("non-http schemes and hostless urls are rejected")
{
    CHECK_THROWS_AS(SsrfGuard::validateUrl("file:///etc/passwd"), std::runtime_error);
    CHECK_THROWS_AS(SsrfGuard::validateUrl("ftp://8.8.8.8/"), std::runtime_error);
    CHECK_THROWS_AS(SsrfGuard::validateUrl("not a url"), std::runtime_error);
}

TEST_CASE("public ip literals pass validation")
{
    CHECK_NOTHROW(SsrfGuard::validateUrl("http://8.8.8.8/"));
    CHECK_NOTHROW(SsrfGuard::validateUrl("https://1.1.1.1/path?q=1"));
}

TEST_CASE("loopback is accepted only when explicitly allowed")
{
    CHECK_THROWS_AS(SsrfGuard::validateUrl("http://127.0.0.1:8080/"), std::runtime_error);
    CHECK_NOTHROW(SsrfGuard::validateUrlAllowLoopback("http://127.0.0.1:8080/"));

    // a non-loopback private range stays blocked even in the loopback-allowed variant
    CHECK_THROWS_AS(SsrfGuard::validateUrlAllowLoopback("http://10.0.0.5/"), std::runtime_error);
}
