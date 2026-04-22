#pragma once

#include "trading/core/types.hpp"
#include "trading/exec/order_types.hpp"

#include <array>

namespace trading {

struct alignas(64) PnlState {
    double cash = 0.0;
    double position = 0.0;
    double avg_entry = 0.0;
    double realized = 0.0;
};

class PnlTracker {
public:
    void seedCash(SymbolId id, double cash) noexcept {
        if (id < MAX_SYMBOLS) states_[id].cash = cash;
    }

    const PnlState& state(SymbolId id) const noexcept { return states_[id]; }

    void onFill(const ExecutionReport& r, double tickSize) noexcept {
        if (!r.isFill || r.symbolId >= MAX_SYMBOLS) return;
        auto& s = states_[r.symbolId];
        double px = r.execPrice.toDouble(tickSize);
        double qty = r.execQty.value();
        if (r.side == Side::Bid) {
            double totalCost = s.avg_entry * s.position + px * qty;
            s.position += qty;
            s.avg_entry = s.position > 0.0 ? totalCost / s.position : 0.0;
            s.cash -= px * qty;
        } else {
            s.realized += (px - s.avg_entry) * qty;
            s.position -= qty;
            s.cash += px * qty;
            if (s.position <= 1e-12) {
                s.position = 0.0;
                s.avg_entry = 0.0;
            }
        }
    }

    double unrealized(SymbolId id, Price topBid, double tickSize) const noexcept {
        const auto& s = states_[id];
        if (s.position == 0.0) return 0.0;
        return s.position * (topBid.toDouble(tickSize) - s.avg_entry);
    }

private:
    std::array<PnlState, MAX_SYMBOLS> states_{};
};

} // namespace trading
