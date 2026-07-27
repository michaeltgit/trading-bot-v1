#include "trading/strategy/imbalance_strategy.hpp"

#include "trading/strategy/order_sizer.hpp"

namespace trading {

ImbalanceStrategy::ImbalanceStrategy(const StrategyParams& params) noexcept : params_(params) {
    if (params_.smoothing_window == 0) params_.smoothing_window = 1;
    if (params_.smoothing_window > MAX_ROLLING) params_.smoothing_window = MAX_ROLLING;
    if (params_.depth_levels == 0) params_.depth_levels = 1;
    if (params_.depth_levels > MAX_DEPTH) params_.depth_levels = MAX_DEPTH;
}

void ImbalanceStrategy::onFill(const ExecutionReport& r) noexcept {
    pnl_.onFill(r, tick_size_);
}

void ImbalanceStrategy::onReconnect() noexcept {
    rolling_count_ = 0;
    rolling_head_ = 0;
    skip_next_signal_ = true;
}

template <size_t Cap>
Signal ImbalanceStrategy::onMarketUpdate(const BoundedBook<Cap>& book, Timestamp now) noexcept {
    PriceLevel bidBuf[MAX_DEPTH];
    PriceLevel askBuf[MAX_DEPTH];
    size_t nb = book.depth(Side::Bid, bidBuf, params_.depth_levels);
    size_t na = book.depth(Side::Ask, askBuf, params_.depth_levels);
    if (nb == 0 || na == 0) return Signal::none();

    double bestBid = bidBuf[0].price.toDouble(tick_size_);
    double bestAsk = askBuf[0].price.toDouble(tick_size_);
    if (bestAsk <= bestBid) return Signal::none();  // crossed/locked book

    double mid = (bestBid + bestAsk) * 0.5;
    double spread_bps = (bestAsk - bestBid) / mid * 1e4;

    double bidSz = 0.0, askSz = 0.0;
    for (size_t i = 0; i < nb; ++i)
        bidSz += bidBuf[i].qty.value();
    for (size_t i = 0; i < na; ++i)
        askSz += askBuf[i].qty.value();
    double denom = bidSz + askSz;
    if (denom <= 0.0) return Signal::none();
    double imbalance = (bidSz - askSz) / denom;  // [-1, 1], + means bid-heavy

    rolling_[rolling_head_] = imbalance;
    rolling_head_ = (rolling_head_ + 1) % params_.smoothing_window;
    if (rolling_count_ < params_.smoothing_window) ++rolling_count_;
    double smoothed = 0.0;
    for (size_t i = 0; i < rolling_count_; ++i)
        smoothed += rolling_[i];
    smoothed /= static_cast<double>(rolling_count_);

    if (skip_next_signal_) {
        skip_next_signal_ = false;
        return Signal::none();
    }

    const PnlState& pnl = state();

    // While holding, the only exits are take-profit and stop-loss; we let the
    // predicted move play out instead of round-tripping on every reversion.
    if (pnl.position > 0.0) {
        double move_bps = (bestBid - pnl.avg_entry) / pnl.avg_entry * 1e4;
        if (move_bps >= params_.take_profit_bps || move_bps <= -params_.stop_loss_bps) {
            return emit(OrderSizer::computeOrder(book, tick_size_, Side::Ask, pnl.position), now);
        }
        return Signal::none();
    }
    if (pnl.position < 0.0) {
        double move_bps = (pnl.avg_entry - bestAsk) / pnl.avg_entry * 1e4;
        if (move_bps >= params_.take_profit_bps || move_bps <= -params_.stop_loss_bps) {
            return emit(OrderSizer::computeOrder(book, tick_size_, Side::Bid, -pnl.position), now);
        }
        return Signal::none();
    }

    // Flat: open a position only on a strong, smoothed imbalance with a tight
    // spread, after the cooldown.
    double secsSinceTrade = static_cast<double>((now - last_trade_ns_).count()) / 1e9;
    if (secsSinceTrade < params_.cooldown_s) return Signal::none();
    if (spread_bps > params_.max_spread_bps) return Signal::none();
    if (pnl.cash <= 0.0) return Signal::none();

    double targetQty = params_.trade_fraction * pnl.cash / mid;
    if (smoothed > params_.entry_imbalance) {
        return emit(OrderSizer::computeOrder(book, tick_size_, Side::Bid, targetQty), now);
    }
    if (params_.allow_short && smoothed < -params_.entry_imbalance) {
        return emit(OrderSizer::computeOrder(book, tick_size_, Side::Ask, targetQty), now);
    }
    return Signal::none();
}

Signal ImbalanceStrategy::emit(Signal s, Timestamp now) noexcept {
    if (!s.isActionable()) return Signal::none();
    s.ts = now;
    last_trade_ns_ = now;
    return s;
}

template Signal ImbalanceStrategy::onMarketUpdate<4096>(const BoundedBook<4096>&,
                                                        Timestamp) noexcept;

}  // namespace trading
