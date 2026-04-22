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
    size_t depth_levels = 6;
    size_t rolling_window = 5;

    double buy_imbalance_pct = 75.0;
    double buy_spread_pct = 0.0003;
    double buy_cooldown_s = 20.0;
    double buy_max_fraction = 0.10;

    double sell_imbalance_pct = 25.0;
    double sell_spread_pct = 0.0003;
    double sell_cooldown_s = 10.0;
    double sell_max_fraction = 1.0;
};

} // namespace trading
