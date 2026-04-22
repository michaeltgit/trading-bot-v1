#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "trading/core/types.hpp"

using namespace trading;
using Catch::Matchers::WithinAbs;

TEST_CASE("Price tick round-trip", "[types]") {
    auto p = Price::fromDouble(42.50, 0.01);
    REQUIRE(p.ticks() == 4250);
    REQUIRE_THAT(p.toDouble(0.01), WithinAbs(42.50, 1e-9));
}

TEST_CASE("Price ordering", "[types]") {
    auto a = Price::fromTicks(100);
    auto b = Price::fromTicks(200);
    REQUIRE(a < b);
    REQUIRE(b > a);
    REQUIRE(a == Price::fromTicks(100));
    REQUIRE(a != b);
}

TEST_CASE("Qty arithmetic", "[types]") {
    Qty a{3.0};
    Qty b{2.5};
    REQUIRE((a + b).value() == 5.5);
    REQUIRE((a - b).value() == 0.5);
    a += b;
    REQUIRE(a.value() == 5.5);
}

TEST_CASE("Qty comparisons", "[types]") {
    REQUIRE(Qty{1.0} < Qty{2.0});
    REQUIRE(Qty{0.0}.isZero());
    REQUIRE_FALSE(Qty{0.1}.isZero());
}

TEST_CASE("Side::opposite", "[types]") {
    REQUIRE(opposite(Side::Bid) == Side::Ask);
    REQUIRE(opposite(Side::Ask) == Side::Bid);
}

TEST_CASE("Price near-tick snapping", "[types]") {
    REQUIRE(Price::fromDouble(42.504, 0.01).ticks() == 4250);
    REQUIRE(Price::fromDouble(42.506, 0.01).ticks() == 4251);
}
