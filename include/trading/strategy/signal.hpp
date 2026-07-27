#pragma once

#include "trading/core/types.hpp"

namespace trading {

enum class SignalAction : uint8_t {
    None = 0,
    Buy,
    Sell,
};

struct Signal {
    SignalAction action = SignalAction::None;
    Price price{};
    Qty qty{};
    Timestamp ts{};

    static constexpr Signal none() noexcept { return Signal{}; }
    constexpr bool isActionable() const noexcept { return action != SignalAction::None; }
};

struct StrategyParams {
    size_t depth_levels = 5;
    size_t smoothing_window = 5;

    double entry_imbalance = 0.30;  // |size imbalance| in [0,1] required to open
    double take_profit_bps = 8.0;   // close on favorable move (bps of entry)
    double stop_loss_bps = 8.0;     // close on adverse move (bps of entry)
    double max_spread_bps = 5.0;    // skip entries when spread is wider than this
    double trade_fraction = 0.10;   // fraction of cash committed per entry
    double cooldown_s = 2.0;
    bool allow_short = true;
};

}  // namespace trading
