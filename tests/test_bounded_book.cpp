#include <catch2/catch_test_macros.hpp>

#include "trading/md/bounded_book.hpp"

#include <map>
#include <random>

using namespace trading;

TEST_CASE("BoundedBook empty top-of-book is nullopt", "[book]") {
    BoundedBook<64> b;
    REQUIRE_FALSE(b.topOfBook(Side::Bid).has_value());
    REQUIRE_FALSE(b.topOfBook(Side::Ask).has_value());
}

TEST_CASE("BoundedBook tracks best bid/ask on inserts", "[book]") {
    BoundedBook<64> b;
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
    BoundedBook<64> b;
    b.update(Side::Bid, Price::fromDouble(100.00, 0.01), Qty{1.0});
    b.update(Side::Bid, Price::fromDouble(100.02, 0.01), Qty{1.0});

    auto bb = b.topOfBook(Side::Bid);
    REQUIRE(bb->price == Price::fromDouble(100.02, 0.01));
}

TEST_CASE("BoundedBook removes level on zero qty", "[book]") {
    BoundedBook<64> b;
    b.update(Side::Ask, Price::fromDouble(100.00, 0.01), Qty{1.0});
    b.update(Side::Ask, Price::fromDouble(100.01, 0.01), Qty{1.0});

    b.update(Side::Ask, Price::fromDouble(100.00, 0.01), Qty{0.0});
    auto ba = b.topOfBook(Side::Ask);
    REQUIRE(ba.has_value());
    REQUIRE(ba->price == Price::fromDouble(100.01, 0.01));
}

TEST_CASE("BoundedBook depth walks levels in price order", "[book]") {
    BoundedBook<64> b;
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

TEST_CASE("BoundedBook recenters as the touch drifts", "[book]") {
    BoundedBook<64> b;  // window 64 ticks, recenter margin = 8
    b.update(Side::Bid, Price::fromDouble(100.00, 0.01), Qty{1.0});
    REQUIRE(b.rebaseCount() == 0);

    // walk the bid up until it nears the window edge -> forces a recenter
    for (int i = 1; i <= 30; ++i) {
        b.update(Side::Bid, Price::fromDouble(100.00 + i * 0.01, 0.01), Qty{1.0});
    }
    REQUIRE(b.rebaseCount() >= 1);
    auto bb = b.topOfBook(Side::Bid);
    REQUIRE(bb.has_value());
    REQUIRE(bb->price == Price::fromDouble(100.30, 0.01));
}

TEST_CASE("BoundedBook ignores far-outlier updates", "[book]") {
    BoundedBook<64> b;  // window is only 64 ticks ($0.64)
    b.update(Side::Bid, Price::fromDouble(100.00, 0.01), Qty{1.0});
    b.update(Side::Ask, Price::fromDouble(100.05, 0.01), Qty{1.0});

    // a deep bid $10 below is far outside the window: ignore it, don't recenter
    b.update(Side::Bid, Price::fromDouble(90.00, 0.01), Qty{5.0});
    REQUIRE(b.rebaseCount() == 0);
    auto bb = b.topOfBook(Side::Bid);
    REQUIRE(bb.has_value());
    REQUIRE(bb->price == Price::fromDouble(100.00, 0.01));
}

TEST_CASE("BoundedBook seeded reference centres the window on the mid", "[book]") {
    // Window is 64 ticks. Anchored on the mid, levels $0.20 either side both fit.
    BoundedBook<64> seeded;
    seeded.seedReference(Price::fromDouble(100.00, 0.01));
    seeded.update(Side::Bid, Price::fromDouble(99.80, 0.01), Qty{1.0});
    seeded.update(Side::Ask, Price::fromDouble(100.20, 0.01), Qty{1.0});
    REQUIRE(seeded.topOfBook(Side::Bid).has_value());
    REQUIRE(seeded.topOfBook(Side::Ask).has_value());
    REQUIRE(seeded.rebaseCount() == 0);

    // Unseeded, the first level anchors the window, so the far side falls outside.
    BoundedBook<64> unseeded;
    unseeded.update(Side::Bid, Price::fromDouble(99.80, 0.01), Qty{1.0});
    unseeded.update(Side::Ask, Price::fromDouble(100.20, 0.01), Qty{1.0});
    REQUIRE(unseeded.topOfBook(Side::Bid).has_value());
    REQUIRE_FALSE(unseeded.topOfBook(Side::Ask).has_value());
}

TEST_CASE("BoundedBook seedReference is ignored once the window is anchored", "[book]") {
    BoundedBook<64> b;
    b.update(Side::Bid, Price::fromDouble(100.00, 0.01), Qty{1.0});
    b.seedReference(Price::fromDouble(200.00, 0.01));  // far away; must not move the window
    REQUIRE(b.topOfBook(Side::Bid)->price == Price::fromDouble(100.00, 0.01));
}

TEST_CASE("BoundedBook finds the next best after the touch is removed", "[book]") {
    BoundedBook<4096> b;
    for (int i = 0; i < 5; ++i) {
        b.update(Side::Bid, Price::fromDouble(100.00 - i * 0.01, 0.01), Qty{1.0});
        b.update(Side::Ask, Price::fromDouble(100.10 + i * 0.01, 0.01), Qty{1.0});
    }

    b.update(Side::Bid, Price::fromDouble(100.00, 0.01), Qty{0.0});
    REQUIRE(b.topOfBook(Side::Bid)->price == Price::fromDouble(99.99, 0.01));

    b.update(Side::Ask, Price::fromDouble(100.10, 0.01), Qty{0.0});
    REQUIRE(b.topOfBook(Side::Ask)->price == Price::fromDouble(100.11, 0.01));

    // Drain each side entirely; both must report empty rather than a stale index.
    for (int i = 1; i < 5; ++i) {
        b.update(Side::Bid, Price::fromDouble(100.00 - i * 0.01, 0.01), Qty{0.0});
        b.update(Side::Ask, Price::fromDouble(100.10 + i * 0.01, 0.01), Qty{0.0});
    }
    REQUIRE_FALSE(b.topOfBook(Side::Bid).has_value());
    REQUIRE_FALSE(b.topOfBook(Side::Ask).has_value());
}

TEST_CASE("BoundedBook best index survives levels spread across bitmap words", "[book]") {
    // Levels >64 ticks apart land in different words, exercising the summary lookup.
    BoundedBook<4096> b;
    b.update(Side::Bid, Price::fromDouble(100.00, 0.01), Qty{1.0});
    b.update(Side::Bid, Price::fromDouble(98.00, 0.01), Qty{2.0});  // 200 ticks lower
    b.update(Side::Ask, Price::fromDouble(100.01, 0.01), Qty{1.0});
    b.update(Side::Ask, Price::fromDouble(102.00, 0.01), Qty{2.0});  // 199 ticks higher

    REQUIRE(b.topOfBook(Side::Bid)->price == Price::fromDouble(100.00, 0.01));
    REQUIRE(b.topOfBook(Side::Ask)->price == Price::fromDouble(100.01, 0.01));

    b.update(Side::Bid, Price::fromDouble(100.00, 0.01), Qty{0.0});
    b.update(Side::Ask, Price::fromDouble(100.01, 0.01), Qty{0.0});
    REQUIRE(b.topOfBook(Side::Bid)->price == Price::fromDouble(98.00, 0.01));
    REQUIRE(b.topOfBook(Side::Ask)->price == Price::fromDouble(102.00, 0.01));
}

TEST_CASE("BoundedBook matches std::map reference on random inputs", "[book][fuzz]") {
    BoundedBook<4096> book;
    std::map<int64_t, double> ref_bids;
    std::map<int64_t, double> ref_asks;

    std::mt19937 rng(42);
    std::uniform_int_distribution<int64_t> pxOffset(-50, 50);
    std::uniform_real_distribution<double> qty(0.0, 5.0);
    std::uniform_int_distribution<int> sideDist(0, 1);

    const int64_t basePx = 10000;  // $100 @ 0.01 tick

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
