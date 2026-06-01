#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "trading/md/bounded_book.hpp"
#include "trading/strategy/order_sizer.hpp"

using namespace trading;
using Catch::Matchers::WithinAbs;

TEST_CASE("computeOrder on empty book returns none", "[sizer]") {
    BoundedBook<4096> book(0.01);
    auto s = OrderSizer::computeOrder(book, 0.01, Side::Bid, 1.0);
    REQUIRE(s.action == SignalAction::None);
}

TEST_CASE("computeOrder rejects non-positive qty", "[sizer]") {
    BoundedBook<4096> book(0.01);
    book.update(Side::Ask, Price::fromDouble(100.0, 0.01), Qty{1.0});
    REQUIRE(OrderSizer::computeOrder(book, 0.01, Side::Bid, 0.0).action == SignalAction::None);
}

TEST_CASE("computeOrder buy walks the ask side", "[sizer]") {
    BoundedBook<4096> book(0.01);
    book.update(Side::Bid, Price::fromDouble(99.0, 0.01), Qty{100.0});
    book.update(Side::Ask, Price::fromDouble(100.0, 0.01), Qty{100.0});

    auto s = OrderSizer::computeOrder(book, 0.01, Side::Bid, 0.5);
    REQUIRE(s.action == SignalAction::Buy);
    REQUIRE_THAT(s.qty.value(), WithinAbs(0.5, 1e-9));
    REQUIRE(s.price == Price::fromDouble(100.0, 0.01));
}

TEST_CASE("computeOrder sell walks the bid side", "[sizer]") {
    BoundedBook<4096> book(0.01);
    book.update(Side::Bid, Price::fromDouble(99.0, 0.01), Qty{100.0});
    book.update(Side::Ask, Price::fromDouble(100.0, 0.01), Qty{100.0});

    auto s = OrderSizer::computeOrder(book, 0.01, Side::Ask, 2.0);
    REQUIRE(s.action == SignalAction::Sell);
    REQUIRE_THAT(s.qty.value(), WithinAbs(2.0, 1e-9));
    REQUIRE(s.price == Price::fromDouble(99.0, 0.01));
}

TEST_CASE("computeOrder caps at available depth and walks to worst level", "[sizer]") {
    BoundedBook<4096> book(0.01);
    book.update(Side::Ask, Price::fromDouble(100.0, 0.01), Qty{0.3});
    book.update(Side::Ask, Price::fromDouble(100.5, 0.01), Qty{0.2});

    auto s = OrderSizer::computeOrder(book, 0.01, Side::Bid, 5.0);
    REQUIRE(s.action == SignalAction::Buy);
    REQUIRE_THAT(s.qty.value(), WithinAbs(0.5, 1e-9));
    REQUIRE(s.price == Price::fromDouble(100.5, 0.01));
}
