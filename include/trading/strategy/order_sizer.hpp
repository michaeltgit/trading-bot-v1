#pragma once

#include "trading/core/types.hpp"
#include "trading/md/bounded_book.hpp"
#include "trading/strategy/signal.hpp"

namespace trading {

class OrderSizer {
public:
    struct BuyInputs {
        double cash_usd;
        double imbalance_pct;
        double spread;
        double mid_price;
        double max_fraction;
        size_t rolling_window;
        bool history_reversed;
    };

    struct SellInputs {
        double position;
        double imbalance_pct;
        double spread;
        double mid_price;
        double max_fraction;
        size_t rolling_window;
        bool history_reversed;
    };

    template <size_t Cap>
    static Signal computeBuy(const BoundedBook<Cap>& book,
                             double tickSize,
                             const BuyInputs& in) noexcept;

    template <size_t Cap>
    static Signal computeSell(const BoundedBook<Cap>& book,
                              double tickSize,
                              const SellInputs& in) noexcept;

    static double confidenceBuy(double imbalance_pct, double spread, double mid_price) noexcept;
    static double confidenceSell(double imbalance_pct, double spread, double mid_price) noexcept;
};

template <size_t Cap>
Signal OrderSizer::computeBuy(const BoundedBook<Cap>& book, double tickSize, const BuyInputs& in) noexcept {
    if (in.history_reversed) return Signal::none();
    if (in.cash_usd <= 0.0) return Signal::none();

    double conf = confidenceBuy(in.imbalance_pct, in.spread, in.mid_price);
    double targetDollars = in.cash_usd * in.max_fraction * conf;
    if (targetDollars <= 0.0) return Signal::none();

    PriceLevel levels[32];
    size_t n = book.depth(Side::Ask, levels, 32);
    if (n == 0) return Signal::none();

    double accDollars = 0.0;
    double accSize = 0.0;
    double limitPx = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double px = levels[i].price.toDouble(tickSize);
        double lvlSize = levels[i].qty.value();
        double fill = std::min((targetDollars - accDollars) / px, lvlSize);
        if (fill <= 0.0) break;
        accSize += fill;
        accDollars += fill * px;
        limitPx = px;
        if (accDollars >= targetDollars) break;
    }

    if (accSize <= 0.0) return Signal::none();
    Signal s;
    s.action = SignalAction::Buy;
    s.price = Price::fromDouble(limitPx, tickSize);
    s.qty = Qty{accSize};
    return s;
}

template <size_t Cap>
Signal OrderSizer::computeSell(const BoundedBook<Cap>& book, double tickSize, const SellInputs& in) noexcept {
    if (in.history_reversed) return Signal::none();
    if (in.position <= 0.0) return Signal::none();

    double conf = confidenceSell(in.imbalance_pct, in.spread, in.mid_price);
    double target = in.position * in.max_fraction * conf;
    if (target <= 0.0) return Signal::none();

    PriceLevel levels[32];
    size_t n = book.depth(Side::Bid, levels, 32);
    if (n == 0) return Signal::none();

    double accSize = 0.0;
    double limitPx = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double px = levels[i].price.toDouble(tickSize);
        double lvlSize = levels[i].qty.value();
        double fill = std::min(target - accSize, lvlSize);
        if (fill <= 0.0) break;
        accSize += fill;
        limitPx = px;
        if (accSize >= target) break;
    }

    if (accSize <= 0.0) return Signal::none();
    Signal s;
    s.action = SignalAction::Sell;
    s.price = Price::fromDouble(limitPx, tickSize);
    s.qty = Qty{accSize};
    return s;
}

} // namespace trading
