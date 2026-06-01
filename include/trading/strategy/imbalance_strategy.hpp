#pragma once

#include "trading/core/types.hpp"
#include "trading/exec/order_types.hpp"
#include "trading/md/bounded_book.hpp"
#include "trading/strategy/pnl_tracker.hpp"
#include "trading/strategy/signal.hpp"

#include <array>

namespace trading {

class ImbalanceStrategy {
public:
    static constexpr size_t MAX_DEPTH = 32;
    static constexpr size_t MAX_ROLLING = 32;

    explicit ImbalanceStrategy(const StrategyParams& params) noexcept;

    void setSymbol(SymbolId id, double tickSize) noexcept {
        symbol_ = id;
        tick_size_ = tickSize;
    }

    void setInitialCash(double cash) noexcept {
        if (symbol_ < MAX_SYMBOLS) pnl_.seedCash(symbol_, cash);
    }

    template <size_t Cap>
    Signal onMarketUpdate(const BoundedBook<Cap>& book, Timestamp now) noexcept;

    void onFill(const ExecutionReport& r) noexcept;
    void onReconnect() noexcept;

    double cash() const noexcept { return state().cash; }
    double position() const noexcept { return state().position; }
    double avgEntry() const noexcept { return state().avg_entry; }
    double realized() const noexcept { return state().realized; }

private:
    const PnlState& state() const noexcept {
        static constexpr PnlState kEmpty{};
        return symbol_ < MAX_SYMBOLS ? pnl_.state(symbol_) : kEmpty;
    }

    Signal emit(Signal s, Timestamp now) noexcept;

    StrategyParams params_;
    SymbolId symbol_ = INVALID_SYMBOL_ID;
    double tick_size_ = 0.0;

    std::array<double, MAX_ROLLING> rolling_{};
    size_t rolling_count_ = 0;
    size_t rolling_head_ = 0;

    PnlTracker pnl_;

    Timestamp last_trade_ns_{};
    bool skip_next_signal_ = false;
};

} // namespace trading
