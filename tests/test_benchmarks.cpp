#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include "trading/md/bounded_book.hpp"
#include "trading/md/coinbase_parser.hpp"
#include "trading/strategy/imbalance_strategy.hpp"

#include <chrono>
#include <string>

using namespace trading;

TEST_CASE("simdjson parse l2update", "[!benchmark][parser]") {
    CoinbaseParser parser;
    CoinbaseMessage msg;
    const std::string payload = R"({"type":"l2update","product_id":"BTC-USD","time":"2024-01-01T12:00:00.000000Z","changes":[["buy","64999.50","2.0"],["sell","65001.00","0"],["buy","65000.00","0.7"],["buy","64998.50","1.5"],["sell","65001.50","0.5"]]})";

    BENCHMARK("parse l2update with 5 changes") {
        return parser.parse(payload, msg);
    };
}

TEST_CASE("BoundedBook 1M random updates", "[!benchmark][book]") {
    BoundedBook<4096> book(0.01);
    BENCHMARK("single update") {
        book.update(Side::Bid, Price::fromDouble(100.0, 0.01), Qty{1.0});
        return 0;
    };
}

TEST_CASE("Strategy end-to-end tick", "[!benchmark][strategy]") {
    StrategyParams p;
    ImbalanceStrategy strat(p);
    strat.setSymbol(0, 0.01);
    strat.setInitialCash(10'000.0);

    BoundedBook<4096> book(0.01);
    for (int i = 0; i < 6; ++i) {
        book.update(Side::Bid, Price::fromDouble(99.99 - i * 0.01, 0.01), Qty{1.0});
        book.update(Side::Ask, Price::fromDouble(100.00 + i * 0.01, 0.01), Qty{1.0});
    }

    Timestamp ts{std::chrono::seconds{1000}};
    BENCHMARK("strategy tick on balanced book") {
        static double q = 1.0;
        q += 0.01;
        book.update(Side::Bid, Price::fromDouble(99.99, 0.01), Qty{q});
        Signal s = strat.onMarketUpdate(book, ts);
        ts += std::chrono::milliseconds{1};
        return s.qty.value();
    };
}
