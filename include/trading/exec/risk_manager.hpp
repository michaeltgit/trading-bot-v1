#pragma once

#include "trading/core/types.hpp"
#include "trading/exec/order_types.hpp"

#include <array>

namespace trading {

class RiskManager {
public:
    // initialCash < 0 leaves the cash check disabled.
    void configure(SymbolId id, double maxPosition, double initialCash = -1.0,
                   double tickSize = 0.0) noexcept {
        if (id >= MAX_SYMBOLS) return;
        maxPositions_[id] = maxPosition;
        if (initialCash >= 0.0) {
            cash_[id] = initialCash;
            cashEnabled_ = true;
            tick_ = tickSize;
        }
    }

    bool approve(const NewOrder& o) const noexcept {
        if (o.symbolId >= MAX_SYMBOLS) return false;
        double delta = o.qty.value() * (o.side == Side::Bid ? 1.0 : -1.0);
        double projected = positions_[o.symbolId] + delta;
        double maxPos = maxPositions_[o.symbolId];
        if (projected > maxPos || projected < -maxPos) return false;
        if (cashEnabled_ && o.side == Side::Bid) {
            if (o.qty.value() * o.price.toDouble(tick_) > cash_[o.symbolId]) return false;
        }
        return true;
    }

    void onFill(const ExecutionReport& r) noexcept {
        if (!r.isFill || r.symbolId >= MAX_SYMBOLS) return;
        double signedQty = r.execQty.value() * (r.side == Side::Bid ? 1.0 : -1.0);
        positions_[r.symbolId] += signedQty;
        if (cashEnabled_) cash_[r.symbolId] -= signedQty * r.execPrice.toDouble(tick_);
    }

    double position(SymbolId id) const noexcept { return id < MAX_SYMBOLS ? positions_[id] : 0.0; }
    double cash(SymbolId id) const noexcept { return id < MAX_SYMBOLS ? cash_[id] : 0.0; }

private:
    std::array<double, MAX_SYMBOLS> positions_{};
    std::array<double, MAX_SYMBOLS> maxPositions_{};
    std::array<double, MAX_SYMBOLS> cash_{};
    double tick_ = 0.0;
    bool cashEnabled_ = false;
};

}  // namespace trading
