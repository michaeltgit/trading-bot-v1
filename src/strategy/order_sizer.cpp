#include "trading/strategy/order_sizer.hpp"

#include <algorithm>

namespace trading {

double OrderSizer::confidenceBuy(double imbalance_pct, double spread, double mid_price) noexcept {
    if (mid_price <= 0.0) return 0.0;
    double spread_penalty = std::min(spread / mid_price, 0.005);
    double raw_advantage = (imbalance_pct - 50.0) / 50.0;
    double conf = raw_advantage * (1.0 - spread_penalty / 0.01);
    return std::clamp(conf, 0.0, 1.0);
}

double OrderSizer::confidenceSell(double imbalance_pct, double spread, double mid_price) noexcept {
    if (mid_price <= 0.0) return 0.0;
    double spread_penalty = std::min(spread / mid_price, 0.005);
    double raw_advantage = (50.0 - imbalance_pct) / 40.0;
    double conf = raw_advantage * (1.0 - spread_penalty / 0.01);
    return std::clamp(conf, 0.0, 1.0);
}

} // namespace trading
