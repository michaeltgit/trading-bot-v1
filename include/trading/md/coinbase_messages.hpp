#pragma once

#include "trading/core/types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace trading {

enum class CoinbaseMsgType : uint8_t {
    Unknown = 0,
    Subscriptions,
    Snapshot,
    L2Update,
    Heartbeat,
    Error,
};

struct CoinbasePriceLevel {
    double price;
    double qty;
};

struct CoinbaseChange {
    Side side;
    double price;
    double qty;
};

struct CoinbaseMessage {
    CoinbaseMsgType type = CoinbaseMsgType::Unknown;
    std::string product_id;

    std::vector<CoinbasePriceLevel> bids;
    std::vector<CoinbasePriceLevel> asks;
    std::vector<CoinbaseChange> changes;
    uint64_t sequence = 0;
    std::string error_message;
};

}  // namespace trading
