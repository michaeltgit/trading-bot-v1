#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "trading/strategy/pnl_tracker.hpp"

using namespace trading;
using Catch::Matchers::WithinAbs;

namespace {
ExecutionReport buyFill(double px, double qty) {
    ExecutionReport r;
    r.id = 1; r.symbolId = 0; r.side = Side::Bid; r.isFill = true;
    r.execPrice = Price::fromDouble(px, 0.01); r.execQty = Qty{qty};
    return r;
}
ExecutionReport sellFill(double px, double qty) {
    ExecutionReport r;
    r.id = 2; r.symbolId = 0; r.side = Side::Ask; r.isFill = true;
    r.execPrice = Price::fromDouble(px, 0.01); r.execQty = Qty{qty};
    return r;
}
} // namespace

TEST_CASE("PnL buy then sell realizes profit", "[pnl]") {
    PnlTracker t;
    t.seedCash(0, 10'000.0);

    t.onFill(buyFill(100.0, 2.0), 0.01);
    const auto& s1 = t.state(0);
    REQUIRE_THAT(s1.position, WithinAbs(2.0, 1e-9));
    REQUIRE_THAT(s1.avg_entry, WithinAbs(100.0, 1e-9));
    REQUIRE_THAT(s1.cash, WithinAbs(9'800.0, 1e-9));

    t.onFill(sellFill(110.0, 1.0), 0.01);
    const auto& s2 = t.state(0);
    REQUIRE_THAT(s2.position, WithinAbs(1.0, 1e-9));
    REQUIRE_THAT(s2.cash, WithinAbs(9'910.0, 1e-9));
    REQUIRE_THAT(s2.realized, WithinAbs(10.0, 1e-9));
}

TEST_CASE("Two buys compute weighted avg entry", "[pnl]") {
    PnlTracker t;
    t.seedCash(0, 10'000.0);
    t.onFill(buyFill(100.0, 2.0), 0.01);
    t.onFill(buyFill(110.0, 2.0), 0.01);
    REQUIRE_THAT(t.state(0).avg_entry, WithinAbs(105.0, 1e-9));
}

TEST_CASE("Full sell resets avg_entry", "[pnl]") {
    PnlTracker t;
    t.seedCash(0, 1000.0);
    t.onFill(buyFill(100.0, 1.0), 0.01);
    t.onFill(sellFill(100.0, 1.0), 0.01);
    REQUIRE(t.state(0).position == 0.0);
    REQUIRE(t.state(0).avg_entry == 0.0);
}

TEST_CASE("Unrealized uses top bid", "[pnl]") {
    PnlTracker t;
    t.seedCash(0, 1000.0);
    t.onFill(buyFill(100.0, 2.0), 0.01);
    double u = t.unrealized(0, Price::fromDouble(105.0, 0.01), 0.01);
    REQUIRE_THAT(u, WithinAbs(10.0, 1e-9));
}

TEST_CASE("Cancel report is ignored", "[pnl]") {
    PnlTracker t;
    t.seedCash(0, 1000.0);
    ExecutionReport cxl;
    cxl.id = 1; cxl.symbolId = 0; cxl.side = Side::Bid; cxl.isFill = false;
    t.onFill(cxl, 0.01);
    REQUIRE(t.state(0).position == 0.0);
    REQUIRE(t.state(0).cash == 1000.0);
}
