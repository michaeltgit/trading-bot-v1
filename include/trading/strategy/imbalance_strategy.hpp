#pragma once

#include "trading/core/types.hpp"
#include "trading/exec/order_types.hpp"
#include "trading/md/bounded_book.hpp"
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

    void setInitialCash(double cash) noexcept { cash_ = cash; }

    template <size_t Cap>
    Signal onMarketUpdate(const BoundedBook<Cap>& book, Timestamp now) noexcept;

    void onFill(const ExecutionReport& r) noexcept;
    void onReconnect() noexcept;

    double cash() const noexcept { return cash_; }
    double position() const noexcept { return position_; }
    double avgEntry() const noexcept { return avg_entry_; }
    double realized() const noexcept { return realized_; }

private:
    StrategyParams params_;
    SymbolId symbol_ = INVALID_SYMBOL_ID;
    double tick_size_ = 0.0;

    std::array<double, MAX_ROLLING> rolling_{};
    size_t rolling_count_ = 0;
    size_t rolling_head_ = 0;

    double prev_bid_ = 0.0;
    double prev_bid_sz_ = 0.0;
    double prev_ask_ = 0.0;
    double prev_ask_sz_ = 0.0;

    double cash_ = 0.0;
    double position_ = 0.0;
    double avg_entry_ = 0.0;
    double realized_ = 0.0;

    Timestamp last_buy_ns_{};
    Timestamp last_sell_ns_{};

    bool skip_next_signal_ = false;
};

} // namespace trading
