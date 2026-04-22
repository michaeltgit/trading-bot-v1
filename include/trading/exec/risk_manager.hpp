#pragma once

#include "trading/core/types.hpp"
#include "trading/exec/order_types.hpp"

#include <array>
#include <atomic>

namespace trading {

class RiskManager {
public:
    void configure(SymbolId id, double maxPosition) noexcept {
        if (id < MAX_SYMBOLS) maxPositions_[id] = maxPosition;
    }

    bool approve(const NewOrder& o) const noexcept {
        if (o.symbolId >= MAX_SYMBOLS) return false;
        double cur = positions_[o.symbolId];
        double delta = o.qty.value() * (o.side == Side::Bid ? 1.0 : -1.0);
        double projected = cur + delta;
        double maxPos = maxPositions_[o.symbolId];
        return projected <= maxPos && projected >= -maxPos;
    }

    void onFill(const ExecutionReport& r) noexcept {
        if (!r.isFill || r.symbolId >= MAX_SYMBOLS) return;
        double delta = r.execQty.value() * (r.side == Side::Bid ? 1.0 : -1.0);
        positions_[r.symbolId] += delta;
    }

    double position(SymbolId id) const noexcept {
        return id < MAX_SYMBOLS ? positions_[id] : 0.0;
    }

private:
    std::array<double, MAX_SYMBOLS> positions_{};
    std::array<double, MAX_SYMBOLS> maxPositions_{};
};

} // namespace trading
