#include <catch2/catch_test_macros.hpp>

#include "trading/md/coinbase_parser.hpp"

#include <fstream>
#include <sstream>
#include <string>

using namespace trading;

namespace {
std::string readFile(const char* name) {
    std::string full = std::string(TRADING_FIXTURES_DIR) + "/" + name;
    std::ifstream in(full);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
}  // namespace

TEST_CASE("Parses subscriptions ack", "[coinbase][parser]") {
    CoinbaseParser p;
    CoinbaseMessage msg;
    REQUIRE(p.parse(R"({"type":"subscriptions","channels":[]})", msg) == Error::Ok);
    REQUIRE(msg.type == CoinbaseMsgType::Subscriptions);
}

TEST_CASE("Parses snapshot fixture", "[coinbase][parser]") {
    CoinbaseParser p;
    CoinbaseMessage msg;
    auto s = readFile("coinbase_snapshot.json");
    REQUIRE(p.parse(s, msg) == Error::Ok);
    REQUIRE(msg.type == CoinbaseMsgType::Snapshot);
    REQUIRE(msg.product_id == "BTC-USD");
    REQUIRE(msg.bids.size() == 2);
    REQUIRE(msg.asks.size() == 2);
    REQUIRE(msg.bids[0].price == 65000.00);
    REQUIRE(msg.bids[0].qty == 0.5);
    REQUIRE(msg.asks[0].price == 65001.00);
}

TEST_CASE("Parses l2update fixture", "[coinbase][parser]") {
    CoinbaseParser p;
    CoinbaseMessage msg;
    auto s = readFile("coinbase_l2update.json");
    REQUIRE(p.parse(s, msg) == Error::Ok);
    REQUIRE(msg.type == CoinbaseMsgType::L2Update);
    REQUIRE(msg.changes.size() == 3);
    REQUIRE(msg.changes[0].side == Side::Bid);
    REQUIRE(msg.changes[0].price == 64999.50);
    REQUIRE(msg.changes[1].side == Side::Ask);
    REQUIRE(msg.changes[1].qty == 0.0);  // removal
}

TEST_CASE("Parses heartbeat fixture", "[coinbase][parser]") {
    CoinbaseParser p;
    CoinbaseMessage msg;
    auto s = readFile("coinbase_heartbeat.json");
    REQUIRE(p.parse(s, msg) == Error::Ok);
    REQUIRE(msg.type == CoinbaseMsgType::Heartbeat);
    REQUIRE(msg.sequence == 987654321);
}

TEST_CASE("Parses error message", "[coinbase][parser]") {
    CoinbaseParser p;
    CoinbaseMessage msg;
    REQUIRE(p.parse(R"({"type":"error","message":"bad product"})", msg) == Error::Ok);
    REQUIRE(msg.type == CoinbaseMsgType::Error);
    REQUIRE(msg.error_message == "bad product");
}

TEST_CASE("Rejects malformed JSON", "[coinbase][parser]") {
    CoinbaseParser p;
    CoinbaseMessage msg;
    REQUIRE(p.parse("{not-json", msg) == Error::ParseMalformedJson);
}

TEST_CASE("Rejects missing type field", "[coinbase][parser]") {
    CoinbaseParser p;
    CoinbaseMessage msg;
    REQUIRE(p.parse(R"({"foo":"bar"})", msg) == Error::ParseUnexpectedShape);
}

TEST_CASE("Parser reuses buffers across calls", "[coinbase][parser]") {
    CoinbaseParser p;
    CoinbaseMessage msg;
    auto snap = readFile("coinbase_snapshot.json");
    auto upd = readFile("coinbase_l2update.json");

    REQUIRE(p.parse(snap, msg) == Error::Ok);
    REQUIRE(msg.bids.size() == 2);

    REQUIRE(p.parse(upd, msg) == Error::Ok);
    REQUIRE(msg.bids.empty());  // cleared
    REQUIRE(msg.changes.size() == 3);
}
