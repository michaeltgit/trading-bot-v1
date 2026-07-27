#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "trading/exec/execution_engine.hpp"
#include "trading/md/bounded_book.hpp"
#include "trading/strategy/order_sizer.hpp"

using namespace trading;
using Catch::Matchers::WithinAbs;

namespace {
NewOrder mk(OrderId id, Side s, double px, double qty, double tick) {
    NewOrder o{};
    o.id = id;
    o.symbolId = 0;
    o.side = s;
    o.price = Price::fromDouble(px, tick);
    o.qty = Qty{qty};
    return o;
}
}  // namespace

TEST_CASE("Full fill at a single level", "[engine]") {
    BoundedBook<4096> book;
    book.update(Side::Ask, Price::fromDouble(100.0, 0.01), Qty{5.0});

    ExecutionEngine eng;
    auto rpt = eng.simulate(mk(1, Side::Bid, 100.0, 5.0, 0.01), book, 0.01, Timestamp{});
    REQUIRE(rpt.isFill);
    REQUIRE_THAT(rpt.execQty.value(), WithinAbs(5.0, 1e-9));
    REQUIRE(rpt.execPrice == Price::fromDouble(100.0, 0.01));
}

TEST_CASE("Partial fill when depth insufficient", "[engine]") {
    BoundedBook<4096> book;
    book.update(Side::Ask, Price::fromDouble(100.0, 0.01), Qty{3.0});

    ExecutionEngine eng;
    auto rpt = eng.simulate(mk(1, Side::Bid, 100.0, 10.0, 0.01), book, 0.01, Timestamp{});
    REQUIRE(rpt.isFill);
    REQUIRE_THAT(rpt.execQty.value(), WithinAbs(3.0, 1e-9));
}

TEST_CASE("VWAP across multiple levels", "[engine]") {
    BoundedBook<4096> book;
    book.update(Side::Ask, Price::fromDouble(100.00, 0.01), Qty{5.0});
    book.update(Side::Ask, Price::fromDouble(100.10, 0.01), Qty{5.0});

    ExecutionEngine eng;
    auto rpt = eng.simulate(mk(1, Side::Bid, 100.10, 10.0, 0.01), book, 0.01, Timestamp{});
    REQUIRE(rpt.isFill);
    REQUIRE_THAT(rpt.execPrice.toDouble(0.01), WithinAbs(100.05, 1e-6));
}

TEST_CASE("No match when limit price is inferior", "[engine]") {
    BoundedBook<4096> book;
    book.update(Side::Ask, Price::fromDouble(100.0, 0.01), Qty{5.0});

    ExecutionEngine eng;
    auto rpt = eng.simulate(mk(1, Side::Bid, 99.0, 5.0, 0.01), book, 0.01, Timestamp{});
    REQUIRE_FALSE(rpt.isFill);
    REQUIRE(rpt.execQty.value() == 0.0);
}

TEST_CASE("Sell matches bid side", "[engine]") {
    BoundedBook<4096> book;
    book.update(Side::Bid, Price::fromDouble(99.0, 0.01), Qty{5.0});

    ExecutionEngine eng;
    auto rpt = eng.simulate(mk(1, Side::Ask, 99.0, 5.0, 0.01), book, 0.01, Timestamp{});
    REQUIRE(rpt.isFill);
    REQUIRE(rpt.side == Side::Ask);
}

TEST_CASE("Full fill across 25 thin levels", "[engine]") {
    BoundedBook<4096> book;
    for (int i = 0; i < 25; ++i) {
        book.update(Side::Ask, Price::fromDouble(100.00 + i * 0.01, 0.01), Qty{1.0});
    }

    ExecutionEngine eng;
    auto rpt = eng.simulate(mk(1, Side::Bid, 100.24, 25.0, 0.01), book, 0.01, Timestamp{});
    REQUIRE(rpt.isFill);
    REQUIRE_THAT(rpt.execQty.value(), WithinAbs(25.0, 1e-9));
}

TEST_CASE("Orders sized by OrderSizer fill completely", "[engine]") {
    BoundedBook<4096> book;
    for (int i = 0; i < 30; ++i) {
        book.update(Side::Ask, Price::fromDouble(100.00 + i * 0.01, 0.01), Qty{0.1});
    }

    auto sized = OrderSizer::computeOrder(book, 0.01, Side::Bid, 2.5);
    REQUIRE(sized.action == SignalAction::Buy);

    ExecutionEngine eng;
    auto rpt = eng.simulate(mk(1, Side::Bid, sized.price.toDouble(0.01), sized.qty.value(), 0.01),
                            book, 0.01, Timestamp{});
    REQUIRE_THAT(rpt.execQty.value(), WithinAbs(sized.qty.value(), 1e-9));
}
