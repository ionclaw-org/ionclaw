#include "doctest/doctest.h"

#include "ionclaw/util/HttpClient.hpp"

// regression guard: a tls context without a working ca store throws an ssl error on every https call,
// which silently breaks all providers and web tools, so verify a real https request succeeds
TEST_CASE("outbound https connects and verifies against the system ca store")
{
    auto response = ionclaw::util::HttpClient::request("GET", "https://example.com", {}, "", 20, true);
    CHECK(response.statusCode == 200);
    CHECK(response.body.find("Example Domain") != std::string::npos);
}
