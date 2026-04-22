#include <catch2/catch_test_macros.hpp>

#include "trading/md/bounded_book.hpp"

#include <map>
#include <random>

using namespace trading;

TEST_CASE("BoundedBook empty top-of-book is nullopt", "[book]") {
    BoundedBook<64> b(0.01);
    REQUIRE_FALSE(b.topOfBook(Side::Bid).has_value());
    REQUIRE_FALSE(b.topOfBook(Side::Ask).has_value());
}

TEST_CASE("BoundedBook tracks best bid/ask on inserts", "[book]") {
    BoundedBook<64> b(0.01);
    b.update(Side::Bid, Price::fromDouble(100.00, 0.01), Qty{1.0});
    b.update(Side::Ask, Price::fromDouble(100.05, 0.01), Qty{2.0});

    auto bb = b.topOfBook(Side::Bid);
    auto ba = b.topOfBook(Side::Ask);
    REQUIRE(bb.has_value());
    REQUIRE(ba.has_value());
    REQUIRE(bb->price == Price::fromDouble(100.00, 0.01));
    REQUIRE(ba->price == Price::fromDouble(100.05, 0.01));
}

TEST_CASE("BoundedBook promotes higher bid as best", "[book]") {
    BoundedBook<64> b(0.01);
    b.update(Side::Bid, Price::fromDouble(100.00, 0.01), Qty{1.0});
    b.update(Side::Bid, Price::fromDouble(100.02, 0.01), Qty{1.0});

    auto bb = b.topOfBook(Side::Bid);
    REQUIRE(bb->price == Price::fromDouble(100.02, 0.01));
}

TEST_CASE("BoundedBook removes level on zero qty", "[book]") {
    BoundedBook<64> b(0.01);
    b.update(Side::Ask, Price::fromDouble(100.00, 0.01), Qty{1.0});
    b.update(Side::Ask, Price::fromDouble(100.01, 0.01), Qty{1.0});

    b.update(Side::Ask, Price::fromDouble(100.00, 0.01), Qty{0.0});
    auto ba = b.topOfBook(Side::Ask);
    REQUIRE(ba.has_value());
    REQUIRE(ba->price == Price::fromDouble(100.01, 0.01));
}

TEST_CASE("BoundedBook depth walks levels in price order", "[book]") {
    BoundedBook<64> b(0.01);
    b.update(Side::Ask, Price::fromDouble(100.02, 0.01), Qty{2.0});
    b.update(Side::Ask, Price::fromDouble(100.00, 0.01), Qty{1.0});
    b.update(Side::Ask, Price::fromDouble(100.04, 0.01), Qty{3.0});

    PriceLevel buf[10];
    size_t n = b.depth(Side::Ask, buf, 10);
    REQUIRE(n == 3);
    REQUIRE(buf[0].price == Price::fromDouble(100.00, 0.01));
    REQUIRE(buf[1].price == Price::fromDouble(100.02, 0.01));
    REQUIRE(buf[2].price == Price::fromDouble(100.04, 0.01));
}

TEST_CASE("BoundedBook rebases on large price drift", "[book]") {
    BoundedBook<64> b(0.01); // REBASE_THRESHOLD = 16 ticks
    b.update(Side::Bid, Price::fromDouble(100.00, 0.01), Qty{1.0});
    REQUIRE(b.rebaseCount() == 0);

    b.update(Side::Ask, Price::fromDouble(110.00, 0.01), Qty{1.0});
    REQUIRE(b.rebaseCount() == 1);

    auto ba = b.topOfBook(Side::Ask);
    REQUIRE(ba.has_value());
    REQUIRE(ba->price == Price::fromDouble(110.00, 0.01));
}

TEST_CASE("BoundedBook matches std::map reference on random inputs", "[book][fuzz]") {
    BoundedBook<4096> book(0.01);
    std::map<int64_t, double> ref_bids;
    std::map<int64_t, double> ref_asks;

    std::mt19937 rng(42);
    std::uniform_int_distribution<int64_t> pxOffset(-50, 50);
    std::uniform_real_distribution<double> qty(0.0, 5.0);
    std::uniform_int_distribution<int> sideDist(0, 1);

    const int64_t basePx = 10000; // $100 @ 0.01 tick

    for (int i = 0; i < 5000; ++i) {
        Side s = (sideDist(rng) == 0) ? Side::Bid : Side::Ask;
        int64_t ticks = basePx + pxOffset(rng);
        double q = qty(rng);

        book.update(s, Price::fromTicks(ticks), Qty{q});
        auto& ref = (s == Side::Bid) ? ref_bids : ref_asks;
        if (q > 0.0) {
            ref[ticks] = q;
        } else {
            ref.erase(ticks);
        }
    }

    auto bb = book.topOfBook(Side::Bid);
    auto ba = book.topOfBook(Side::Ask);
    if (!ref_bids.empty()) {
        REQUIRE(bb.has_value());
        REQUIRE(bb->price.ticks() == ref_bids.rbegin()->first);
    }
    if (!ref_asks.empty()) {
        REQUIRE(ba.has_value());
        REQUIRE(ba->price.ticks() == ref_asks.begin()->first);
    }
}
