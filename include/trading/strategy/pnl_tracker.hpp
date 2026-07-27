#pragma once

#include "trading/core/types.hpp"
#include "trading/exec/order_types.hpp"

#include <algorithm>
#include <array>
#include <cmath>

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
        double signedQty = r.execQty.value() * (r.side == Side::Bid ? 1.0 : -1.0);
        double pos = s.position;
        double newPos = pos + signedQty;

        s.cash -= signedQty * px;

        const bool sameDirection = (pos == 0.0) || ((pos > 0.0) == (signedQty > 0.0));
        if (sameDirection) {
            double totalCost = s.avg_entry * std::abs(pos) + px * std::abs(signedQty);
            s.avg_entry = std::abs(newPos) > 1e-12 ? totalCost / std::abs(newPos) : 0.0;
        } else {
            double closeQty = std::min(std::abs(signedQty), std::abs(pos));
            double direction = (pos > 0.0) ? 1.0 : -1.0;
            s.realized += direction * (px - s.avg_entry) * closeQty;
            const bool flipped = (newPos != 0.0) && ((newPos > 0.0) != (pos > 0.0));
            if (flipped) {
                s.avg_entry = px;
            } else if (std::abs(newPos) <= 1e-12) {
                s.avg_entry = 0.0;
            }
        }
        s.position = std::abs(newPos) <= 1e-12 ? 0.0 : newPos;
    }

    double unrealized(SymbolId id, Price mark, double tickSize) const noexcept {
        const auto& s = states_[id];
        if (s.position == 0.0) return 0.0;
        return s.position * (mark.toDouble(tickSize) - s.avg_entry);
    }

private:
    std::array<PnlState, MAX_SYMBOLS> states_{};
};

}  // namespace trading
