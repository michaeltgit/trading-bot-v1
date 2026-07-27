#include <catch2/catch_test_macros.hpp>

#include "trading/md/bounded_book.hpp"
#include "trading/md/coinbase_parser.hpp"
#include "trading/strategy/imbalance_strategy.hpp"

#include <chrono>
#include <fstream>
#include <string>
#include <vector>

using namespace trading;

namespace {

std::vector<std::string> readLines(const char* name) {
    std::string path = std::string(TRADING_FIXTURES_DIR) + "/" + name;
    std::ifstream in(path);
    std::vector<std::string> out;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) out.push_back(line);
    }
    return out;
}

}  // namespace

TEST_CASE("Replay a recorded tick stream through the strategy", "[replay]") {
    auto lines = readLines("btcusd_replay.jsonl");
    REQUIRE_FALSE(lines.empty());

    CoinbaseParser parser;
    BoundedBook<4096> book;
    StrategyParams params;
    ImbalanceStrategy strategy(params);
    strategy.setSymbol(0, 0.01);
    strategy.setInitialCash(10'000.0);

    CoinbaseMessage msg;
    int actionableSignals = 0;
    Timestamp ts{std::chrono::seconds{100}};

    for (auto& l : lines) {
        REQUIRE(parser.parse(l, msg) == Error::Ok);

        switch (msg.type) {
            case CoinbaseMsgType::Snapshot:
                book.clear();
                for (const auto& b : msg.bids) {
                    book.update(Side::Bid, Price::fromDouble(b.price, 0.01), Qty{b.qty});
                }
                for (const auto& a : msg.asks) {
                    book.update(Side::Ask, Price::fromDouble(a.price, 0.01), Qty{a.qty});
                }
                break;
            case CoinbaseMsgType::L2Update:
                for (const auto& c : msg.changes) {
                    book.update(c.side, Price::fromDouble(c.price, 0.01), Qty{c.qty});
                }
                break;
            default: continue;
        }

        Signal s = strategy.onMarketUpdate(book, ts);
        if (s.isActionable()) ++actionableSignals;
        ts += std::chrono::seconds{1};
    }

    INFO("actionable signals: " << actionableSignals);
    REQUIRE(actionableSignals >= 1);
}

TEST_CASE("Replay is deterministic across runs", "[replay]") {
    auto lines = readLines("btcusd_replay.jsonl");

    auto run = [&]() -> std::vector<double> {
        CoinbaseParser parser;
        BoundedBook<4096> book;
        StrategyParams params;
        ImbalanceStrategy strategy(params);
        strategy.setSymbol(0, 0.01);
        strategy.setInitialCash(10'000.0);
        CoinbaseMessage msg;
        Timestamp ts{std::chrono::seconds{100}};
        std::vector<double> signalQtys;
        for (auto& l : lines) {
            parser.parse(l, msg);
            if (msg.type == CoinbaseMsgType::Snapshot) {
                book.clear();
                for (const auto& b : msg.bids)
                    book.update(Side::Bid, Price::fromDouble(b.price, 0.01), Qty{b.qty});
                for (const auto& a : msg.asks)
                    book.update(Side::Ask, Price::fromDouble(a.price, 0.01), Qty{a.qty});
            } else if (msg.type == CoinbaseMsgType::L2Update) {
                for (const auto& c : msg.changes)
                    book.update(c.side, Price::fromDouble(c.price, 0.01), Qty{c.qty});
            }
            Signal s = strategy.onMarketUpdate(book, ts);
            signalQtys.push_back(s.qty.value());
            ts += std::chrono::seconds{1};
        }
        return signalQtys;
    };

    auto a = run();
    auto b = run();
    REQUIRE(a == b);
}
