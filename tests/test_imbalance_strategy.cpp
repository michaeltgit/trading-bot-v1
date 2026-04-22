#include <catch2/catch_test_macros.hpp>

#include "trading/md/bounded_book.hpp"
#include "trading/strategy/imbalance_strategy.hpp"

#include <chrono>

using namespace trading;

namespace {

StrategyParams params() {
    StrategyParams p;
    p.depth_levels = 6;
    p.rolling_window = 5;
    p.buy_imbalance_pct = 75.0;
    p.sell_imbalance_pct = 25.0;
    p.buy_spread_pct = 0.0003;
    p.sell_spread_pct = 0.0003;
    p.buy_cooldown_s = 20.0;
    p.sell_cooldown_s = 10.0;
    p.buy_max_fraction = 0.10;
    p.sell_max_fraction = 1.0;
    return p;
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

} // namespace

TEST_CASE("Empty book produces no signal", "[strategy]") {
    ImbalanceStrategy s(params());
    s.setSymbol(0, 0.01);
    s.setInitialCash(10'000.0);

    BoundedBook<4096> book(0.01);
    Signal sig = s.onMarketUpdate(book, Timestamp{});
    REQUIRE(sig.action == SignalAction::None);
}

TEST_CASE("First tick after reconnect is skipped", "[strategy]") {
    ImbalanceStrategy s(params());
    s.setSymbol(0, 0.01);
    s.setInitialCash(10'000.0);

    BoundedBook<4096> book(0.01);
    seedBidHeavy(book);

    s.onReconnect();
    Signal sig = s.onMarketUpdate(book, std::chrono::seconds{1000});
    REQUIRE(sig.action == SignalAction::None);
}

TEST_CASE("Balanced book produces no buy signal", "[strategy]") {
    ImbalanceStrategy s(params());
    s.setSymbol(0, 0.01);
    s.setInitialCash(10'000.0);

    BoundedBook<4096> book(0.01);
    seedBalancedBook(book);

    Signal sig = s.onMarketUpdate(book, std::chrono::seconds{1000});
    REQUIRE(sig.action == SignalAction::None);
}

TEST_CASE("Unchanged book produces no signal", "[strategy]") {
    ImbalanceStrategy s(params());
    s.setSymbol(0, 0.01);
    s.setInitialCash(10'000.0);

    BoundedBook<4096> book(0.01);
    seedBidHeavy(book);

    (void)s.onMarketUpdate(book, std::chrono::seconds{100});
    Signal sig = s.onMarketUpdate(book, std::chrono::seconds{200});
    REQUIRE(sig.action == SignalAction::None);
}

TEST_CASE("onFill updates internal position and cash", "[strategy]") {
    ImbalanceStrategy s(params());
    s.setSymbol(0, 0.01);
    s.setInitialCash(1000.0);

    ExecutionReport r;
    r.id = 1; r.symbolId = 0; r.side = Side::Bid; r.isFill = true;
    r.execPrice = Price::fromDouble(100.0, 0.01); r.execQty = Qty{2.0};
    s.onFill(r);

    REQUIRE(s.position() == 2.0);
    REQUIRE(s.cash() == 800.0);
    REQUIRE(s.avgEntry() == 100.0);
}
