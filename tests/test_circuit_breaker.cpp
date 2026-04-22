#include <catch2/catch_test_macros.hpp>

#include "trading/exec/circuit_breaker.hpp"

using namespace trading;

TEST_CASE("CircuitBreaker starts untripped", "[breaker]") {
    CircuitBreaker b(3, 100);
    REQUIRE_FALSE(b.isTripped());
}

TEST_CASE("CircuitBreaker trips at threshold", "[breaker]") {
    CircuitBreaker b(3, 100);
    b.recordError();
    b.recordError();
    REQUIRE_FALSE(b.isTripped());
    b.recordError();
    REQUIRE(b.isTripped());
}

TEST_CASE("CircuitBreaker resets on demand", "[breaker]") {
    CircuitBreaker b(2, 100);
    b.recordError(); b.recordError();
    REQUIRE(b.isTripped());
    b.reset();
    REQUIRE_FALSE(b.isTripped());
}

TEST_CASE("CircuitBreaker window rolls errors", "[breaker]") {
    CircuitBreaker b(5, 10);
    for (int i = 0; i < 4; ++i) b.recordError();
    REQUIRE_FALSE(b.isTripped());
    for (int i = 0; i < 10; ++i) b.recordMessage();
    for (int i = 0; i < 4; ++i) b.recordError();
    REQUIRE_FALSE(b.isTripped());
}
