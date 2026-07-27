#pragma once

#include "trading/core/types.hpp"
#include "trading/md/bounded_book.hpp"
#include "trading/strategy/signal.hpp"

#include <algorithm>

namespace trading {

class OrderSizer {
public:
    // Builds a marketable order for targetQty by walking the opposing book and
    // setting the limit at the worst level it would touch.
    template <size_t Cap>
    static Signal computeOrder(const BoundedBook<Cap>& book, double tickSize, Side side,
                               double targetQty) noexcept;
};

template <size_t Cap>
Signal OrderSizer::computeOrder(const BoundedBook<Cap>& book, double tickSize, Side side,
                                double targetQty) noexcept {
    if (targetQty <= 0.0) return Signal::none();

    Side liquidity = (side == Side::Bid) ? Side::Ask : Side::Bid;
    PriceLevel levels[MAX_BOOK_WALK];
    size_t n = book.depth(liquidity, levels, MAX_BOOK_WALK);
    if (n == 0) return Signal::none();

    double accSize = 0.0;
    double limitPx = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double px = levels[i].price.toDouble(tickSize);
        double fill = std::min(targetQty - accSize, levels[i].qty.value());
        if (fill <= 0.0) break;
        accSize += fill;
        limitPx = px;
        if (accSize >= targetQty) break;
    }

    if (accSize <= 0.0) return Signal::none();
    Signal s;
    s.action = (side == Side::Bid) ? SignalAction::Buy : SignalAction::Sell;
    s.price = Price::fromDouble(limitPx, tickSize);
    s.qty = Qty{accSize};
    return s;
}

}  // namespace trading
