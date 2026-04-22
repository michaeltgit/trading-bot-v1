#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "trading/exec/execution_engine.hpp"
#include "trading/md/bounded_book.hpp"

using namespace trading;
using Catch::Matchers::WithinAbs;

namespace {
NewOrder mk(OrderId id, Side s, double px, double qty, double tick) {
    NewOrder o{};
    o.id = id; o.symbolId = 0; o.side = s;
    o.price = Price::fromDouble(px, tick);
    o.qty = Qty{qty};
    return o;
}
} // namespace

TEST_CASE("Full fill at a single level", "[engine]") {
    BoundedBook<4096> book(0.01);
    book.update(Side::Ask, Price::fromDouble(100.0, 0.01), Qty{5.0});

    ExecutionEngine eng;
    auto rpt = eng.simulate(mk(1, Side::Bid, 100.0, 5.0, 0.01), book, 0.01, Timestamp{});
    REQUIRE(rpt.isFill);
    REQUIRE_THAT(rpt.execQty.value(), WithinAbs(5.0, 1e-9));
    REQUIRE(rpt.execPrice == Price::fromDouble(100.0, 0.01));
}

TEST_CASE("Partial fill when depth insufficient", "[engine]") {
    BoundedBook<4096> book(0.01);
    book.update(Side::Ask, Price::fromDouble(100.0, 0.01), Qty{3.0});

    ExecutionEngine eng;
    auto rpt = eng.simulate(mk(1, Side::Bid, 100.0, 10.0, 0.01), book, 0.01, Timestamp{});
    REQUIRE(rpt.isFill);
    REQUIRE_THAT(rpt.execQty.value(), WithinAbs(3.0, 1e-9));
}

TEST_CASE("VWAP across multiple levels", "[engine]") {
    BoundedBook<4096> book(0.01);
    book.update(Side::Ask, Price::fromDouble(100.00, 0.01), Qty{5.0});
    book.update(Side::Ask, Price::fromDouble(100.10, 0.01), Qty{5.0});

    ExecutionEngine eng;
    auto rpt = eng.simulate(mk(1, Side::Bid, 100.10, 10.0, 0.01), book, 0.01, Timestamp{});
    REQUIRE(rpt.isFill);
    REQUIRE_THAT(rpt.execPrice.toDouble(0.01), WithinAbs(100.05, 1e-6));
}

TEST_CASE("No match when limit price is inferior", "[engine]") {
    BoundedBook<4096> book(0.01);
    book.update(Side::Ask, Price::fromDouble(100.0, 0.01), Qty{5.0});

    ExecutionEngine eng;
    auto rpt = eng.simulate(mk(1, Side::Bid, 99.0, 5.0, 0.01), book, 0.01, Timestamp{});
    REQUIRE_FALSE(rpt.isFill);
    REQUIRE(rpt.execQty.value() == 0.0);
}

TEST_CASE("Sell matches bid side", "[engine]") {
    BoundedBook<4096> book(0.01);
    book.update(Side::Bid, Price::fromDouble(99.0, 0.01), Qty{5.0});

    ExecutionEngine eng;
    auto rpt = eng.simulate(mk(1, Side::Ask, 99.0, 5.0, 0.01), book, 0.01, Timestamp{});
    REQUIRE(rpt.isFill);
    REQUIRE(rpt.side == Side::Ask);
}
