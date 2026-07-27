#pragma once

#include "trading/core/types.hpp"
#include "trading/exec/order_types.hpp"
#include "trading/md/bounded_book.hpp"

namespace trading {

class ExecutionEngine {
public:
    template <size_t Cap>
    ExecutionReport simulate(const NewOrder& order, const BoundedBook<Cap>& book, double tickSize,
                             Timestamp now) const noexcept;
};

template <size_t Cap>
ExecutionReport ExecutionEngine::simulate(const NewOrder& order, const BoundedBook<Cap>& book,
                                          double tickSize, Timestamp now) const noexcept {
    ExecutionReport rpt{};
    rpt.id = order.id;
    rpt.symbolId = order.symbolId;
    rpt.side = order.side;
    rpt.completedNs = now;

    PriceLevel levels[MAX_BOOK_WALK];
    Side opposing = opposite(order.side);
    size_t n = book.depth(opposing, levels, MAX_BOOK_WALK);

    double limitPx = order.price.toDouble(tickSize);
    double remaining = order.qty.value();
    double filled = 0.0;
    double cost = 0.0;

    for (size_t i = 0; i < n; ++i) {
        double lvlPx = levels[i].price.toDouble(tickSize);
        double lvlSize = levels[i].qty.value();
        bool match = (order.side == Side::Bid) ? (lvlPx <= limitPx) : (lvlPx >= limitPx);
        if (!match) break;

        double fillSize = (remaining < lvlSize) ? remaining : lvlSize;
        remaining -= fillSize;
        filled += fillSize;
        cost += fillSize * lvlPx;

        if (remaining <= 1e-12) break;
    }

    rpt.isFill = filled > 0.0;
    rpt.execQty = Qty{filled};
    rpt.execPrice = (filled > 0.0) ? Price::fromDouble(cost / filled, tickSize) : order.price;
    return rpt;
}

}  // namespace trading
