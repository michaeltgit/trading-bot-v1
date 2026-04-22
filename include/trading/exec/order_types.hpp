#pragma once

#include "trading/core/types.hpp"

#include <string>

namespace trading {

struct NewOrder {
    OrderId id;
    SymbolId symbolId;
    Side side;
    Price price;
    Qty qty;
    Timestamp createdNs;
};

struct ExecutionReport {
    OrderId id;
    SymbolId symbolId;
    Side side;
    bool isFill;
    Price execPrice;
    Qty execQty;
    Timestamp completedNs;
};

} // namespace trading
