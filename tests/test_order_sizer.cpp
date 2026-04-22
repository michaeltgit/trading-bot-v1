#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "trading/md/bounded_book.hpp"
#include "trading/strategy/order_sizer.hpp"

using namespace trading;
using Catch::Matchers::WithinAbs;

TEST_CASE("confidenceBuy saturates and clamps", "[sizer]") {
    double c = OrderSizer::confidenceBuy(100.0, 1.0, 99.5);
    REQUIRE_THAT(c, WithinAbs(0.5, 1e-9));
}

TEST_CASE("confidenceBuy clamps to zero below threshold", "[sizer]") {
    REQUIRE(OrderSizer::confidenceBuy(40.0, 0.01, 100.0) == 0.0);
}

TEST_CASE("confidenceSell mirrors buy with 40 divisor", "[sizer]") {
    double c = OrderSizer::confidenceSell(0.0, 1.0, 99.5);
    REQUIRE_THAT(c, WithinAbs(0.625, 1e-9));
}

TEST_CASE("computeBuy on empty book returns none", "[sizer]") {
    BoundedBook<4096> book(0.01);
    OrderSizer::BuyInputs in{1000.0, 80.0, 1.0, 100.0, 0.1, 5, false};
    auto s = OrderSizer::computeBuy(book, 0.01, in);
    REQUIRE(s.action == SignalAction::None);
}

TEST_CASE("computeBuy bails when history reversed", "[sizer]") {
    BoundedBook<4096> book(0.01);
    book.update(Side::Ask, Price::fromDouble(100.0, 0.01), Qty{10.0});
    OrderSizer::BuyInputs in{1000.0, 80.0, 1.0, 100.0, 0.1, 5, true};
    auto s = OrderSizer::computeBuy(book, 0.01, in);
    REQUIRE(s.action == SignalAction::None);
}

TEST_CASE("computeBuy produces expected size", "[sizer]") {
    BoundedBook<4096> book(0.01);
    book.update(Side::Bid, Price::fromDouble(99.0, 0.01), Qty{100.0});
    book.update(Side::Ask, Price::fromDouble(100.0, 0.01), Qty{100.0});

    OrderSizer::BuyInputs in{1000.0, 100.0, 1.0, 99.5, 0.1, 5, false};
    auto s = OrderSizer::computeBuy(book, 0.01, in);
    REQUIRE(s.action == SignalAction::Buy);
    REQUIRE_THAT(s.qty.value(), WithinAbs(0.5, 1e-9));
    REQUIRE(s.price == Price::fromDouble(100.0, 0.01));
}

TEST_CASE("computeSell scales with confidence", "[sizer]") {
    BoundedBook<4096> book(0.01);
    book.update(Side::Bid, Price::fromDouble(99.0, 0.01), Qty{100.0});
    book.update(Side::Ask, Price::fromDouble(100.0, 0.01), Qty{100.0});

    OrderSizer::SellInputs in{2.0, 0.0, 1.0, 99.5, 1.0, 5, false};
    auto s = OrderSizer::computeSell(book, 0.01, in);
    REQUIRE(s.action == SignalAction::Sell);
    REQUIRE_THAT(s.qty.value(), WithinAbs(1.25, 1e-9));
}
