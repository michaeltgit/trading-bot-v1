#include <catch2/catch_test_macros.hpp>

#include "trading/exec/risk_manager.hpp"

using namespace trading;

namespace {
NewOrder mkOrder(SymbolId id, Side s, double qty) {
    NewOrder o{};
    o.id = 1; o.symbolId = id; o.side = s;
    o.qty = Qty{qty}; o.price = Price::fromTicks(10000);
    return o;
}
ExecutionReport mkFill(SymbolId id, Side s, double qty) {
    ExecutionReport r{};
    r.id = 1; r.symbolId = id; r.side = s; r.isFill = true;
    r.execQty = Qty{qty}; r.execPrice = Price::fromTicks(10000);
    return r;
}
} // namespace

TEST_CASE("RiskManager approves within limit", "[risk]") {
    RiskManager rm;
    rm.configure(0, 100.0);
    REQUIRE(rm.approve(mkOrder(0, Side::Bid, 50.0)));
}

TEST_CASE("RiskManager rejects over long limit", "[risk]") {
    RiskManager rm;
    rm.configure(0, 100.0);
    REQUIRE_FALSE(rm.approve(mkOrder(0, Side::Bid, 200.0)));
}

TEST_CASE("RiskManager rejects over short limit", "[risk]") {
    RiskManager rm;
    rm.configure(0, 100.0);
    REQUIRE_FALSE(rm.approve(mkOrder(0, Side::Ask, 200.0)));
}

TEST_CASE("RiskManager position tracks fills", "[risk]") {
    RiskManager rm;
    rm.configure(0, 100.0);
    rm.onFill(mkFill(0, Side::Bid, 30.0));
    REQUIRE(rm.position(0) == 30.0);
    rm.onFill(mkFill(0, Side::Ask, 10.0));
    REQUIRE(rm.position(0) == 20.0);
}

TEST_CASE("RiskManager rejects buy exceeding cash", "[risk]") {
    RiskManager rm;
    rm.configure(0, 100.0, 1000.0, 0.01);  // price = 10000 ticks * 0.01 = 100.0
    REQUIRE(rm.approve(mkOrder(0, Side::Bid, 5.0)));    // cost 500 <= 1000
    REQUIRE_FALSE(rm.approve(mkOrder(0, Side::Bid, 20.0)));  // cost 2000 > 1000
}

TEST_CASE("RiskManager cash depletes on fills", "[risk]") {
    RiskManager rm;
    rm.configure(0, 100.0, 1000.0, 0.01);
    rm.onFill(mkFill(0, Side::Bid, 8.0));  // cost 800
    REQUIRE(rm.cash(0) == 200.0);
    REQUIRE_FALSE(rm.approve(mkOrder(0, Side::Bid, 5.0)));  // cost 500 > 200
}

TEST_CASE("RiskManager isolates symbols", "[risk]") {
    RiskManager rm;
    rm.configure(0, 10.0);
    rm.configure(1, 10.0);
    rm.onFill(mkFill(0, Side::Bid, 5.0));
    REQUIRE(rm.position(0) == 5.0);
    REQUIRE(rm.position(1) == 0.0);
}
