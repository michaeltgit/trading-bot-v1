#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "trading/md/bounded_book.hpp"
#include "trading/strategy/imbalance_strategy.hpp"

#include <chrono>

using namespace trading;
using Catch::Matchers::WithinAbs;

namespace {

ImbalanceStrategy makeStrategy() {
    ImbalanceStrategy s(StrategyParams{});
    s.setSymbol(0, 0.01);
    s.setInitialCash(10'000.0);
    return s;
}

void seedBalancedBook(BoundedBook<4096>& book) {
    for (int i = 0; i < 6; ++i) {
        book.update(Side::Bid, Price::fromDouble(99.99 - i * 0.01, 0.01), Qty{1.0});
        book.update(Side::Ask, Price::fromDouble(100.00 + i * 0.01, 0.01), Qty{1.0});
    }
}

void seedBidHeavy(BoundedBook<4096>& book) {
    for (int i = 0; i < 6; ++i) {
        book.update(Side::Bid, Price::fromDouble(99.99 - i * 0.01, 0.01), Qty{10.0});
        book.update(Side::Ask, Price::fromDouble(100.00 + i * 0.01, 0.01), Qty{0.5});
    }
}

void seedAskHeavy(BoundedBook<4096>& book) {
    for (int i = 0; i < 6; ++i) {
        book.update(Side::Bid, Price::fromDouble(99.99 - i * 0.01, 0.01), Qty{0.5});
        book.update(Side::Ask, Price::fromDouble(100.00 + i * 0.01, 0.01), Qty{10.0});
    }
}

ExecutionReport fill(Side side, double px, double qty) {
    ExecutionReport r{};
    r.id = 1;
    r.symbolId = 0;
    r.side = side;
    r.isFill = true;
    r.execPrice = Price::fromDouble(px, 0.01);
    r.execQty = Qty{qty};
    return r;
}

}  // namespace

TEST_CASE("Empty book produces no signal", "[strategy]") {
    auto s = makeStrategy();
    BoundedBook<4096> book;
    REQUIRE(s.onMarketUpdate(book, Timestamp{}).action == SignalAction::None);
}

TEST_CASE("First tick after reconnect is skipped", "[strategy]") {
    auto s = makeStrategy();
    BoundedBook<4096> book;
    seedBidHeavy(book);
    s.onReconnect();
    REQUIRE(s.onMarketUpdate(book, std::chrono::seconds{1000}).action == SignalAction::None);
}

TEST_CASE("Balanced book produces no entry", "[strategy]") {
    auto s = makeStrategy();
    BoundedBook<4096> book;
    seedBalancedBook(book);
    REQUIRE(s.onMarketUpdate(book, std::chrono::seconds{1000}).action == SignalAction::None);
}

TEST_CASE("Bid-heavy book opens a long", "[strategy]") {
    auto s = makeStrategy();
    BoundedBook<4096> book;
    seedBidHeavy(book);
    REQUIRE(s.onMarketUpdate(book, std::chrono::seconds{1000}).action == SignalAction::Buy);
}

TEST_CASE("Ask-heavy book opens a short", "[strategy]") {
    auto s = makeStrategy();
    BoundedBook<4096> book;
    seedAskHeavy(book);
    REQUIRE(s.onMarketUpdate(book, std::chrono::seconds{1000}).action == SignalAction::Sell);
}

TEST_CASE("Long holds while the move is small", "[strategy]") {
    auto s = makeStrategy();
    s.onFill(fill(Side::Bid, 100.00, 1.0));

    BoundedBook<4096> book;
    book.update(Side::Bid, Price::fromDouble(99.99, 0.01), Qty{5.0});
    book.update(Side::Ask, Price::fromDouble(100.00, 0.01), Qty{5.0});
    REQUIRE(s.onMarketUpdate(book, std::chrono::seconds{1000}).action == SignalAction::None);
}

TEST_CASE("Long takes profit on a favorable move", "[strategy]") {
    auto s = makeStrategy();
    s.onFill(fill(Side::Bid, 100.00, 1.0));

    BoundedBook<4096> book;
    book.update(Side::Bid, Price::fromDouble(100.10, 0.01), Qty{5.0});
    book.update(Side::Ask, Price::fromDouble(100.11, 0.01), Qty{5.0});
    Signal sig = s.onMarketUpdate(book, std::chrono::seconds{1000});
    REQUIRE(sig.action == SignalAction::Sell);
    REQUIRE_THAT(sig.qty.value(), WithinAbs(1.0, 1e-9));
}

TEST_CASE("Long stops out on an adverse move", "[strategy]") {
    auto s = makeStrategy();
    s.onFill(fill(Side::Bid, 100.00, 1.0));

    BoundedBook<4096> book;
    book.update(Side::Bid, Price::fromDouble(99.90, 0.01), Qty{5.0});
    book.update(Side::Ask, Price::fromDouble(99.91, 0.01), Qty{5.0});
    REQUIRE(s.onMarketUpdate(book, std::chrono::seconds{1000}).action == SignalAction::Sell);
}

TEST_CASE("onFill updates position and cash via the tracker", "[strategy]") {
    auto s = makeStrategy();
    s.setInitialCash(1000.0);
    s.onFill(fill(Side::Bid, 100.0, 2.0));
    REQUIRE(s.position() == 2.0);
    REQUIRE(s.cash() == 800.0);
    REQUIRE(s.avgEntry() == 100.0);
}
