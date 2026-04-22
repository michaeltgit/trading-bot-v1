#include "trading/strategy/imbalance_strategy.hpp"

#include "trading/strategy/order_sizer.hpp"

namespace trading {

ImbalanceStrategy::ImbalanceStrategy(const StrategyParams& params) noexcept
    : params_(params) {
    if (params_.rolling_window > MAX_ROLLING) params_.rolling_window = MAX_ROLLING;
    if (params_.depth_levels > MAX_DEPTH) params_.depth_levels = MAX_DEPTH;
}

void ImbalanceStrategy::onFill(const ExecutionReport& r) noexcept {
    if (!r.isFill) return;
    double px = r.execPrice.toDouble(tick_size_);
    double qty = r.execQty.value();
    if (r.side == Side::Bid) {
        double totalCost = avg_entry_ * position_ + px * qty;
        position_ += qty;
        avg_entry_ = position_ > 0.0 ? totalCost / position_ : 0.0;
        cash_ -= px * qty;
    } else {
        realized_ += (px - avg_entry_) * qty;
        position_ -= qty;
        cash_ += px * qty;
        if (position_ <= 1e-12) {
            position_ = 0.0;
            avg_entry_ = 0.0;
        }
    }
}

void ImbalanceStrategy::onReconnect() noexcept {
    rolling_count_ = 0;
    rolling_head_ = 0;
    prev_bid_ = 0.0;
    prev_bid_sz_ = 0.0;
    prev_ask_ = 0.0;
    prev_ask_sz_ = 0.0;
    skip_next_signal_ = true;
}

template <size_t Cap>
Signal ImbalanceStrategy::onMarketUpdate(const BoundedBook<Cap>& book, Timestamp now) noexcept {
    PriceLevel bidBuf[MAX_DEPTH];
    PriceLevel askBuf[MAX_DEPTH];
    size_t nb = book.depth(Side::Bid, bidBuf, params_.depth_levels);
    size_t na = book.depth(Side::Ask, askBuf, params_.depth_levels);
    if (nb == 0 || na == 0) return Signal::none();

    double topBid = bidBuf[0].price.toDouble(tick_size_);
    double topBidSz = bidBuf[0].qty.value();
    double topAsk = askBuf[0].price.toDouble(tick_size_);
    double topAskSz = askBuf[0].qty.value();

    if (topBid == prev_bid_ && topBidSz == prev_bid_sz_ &&
        topAsk == prev_ask_ && topAskSz == prev_ask_sz_) {
        return Signal::none();
    }
    prev_bid_ = topBid; prev_bid_sz_ = topBidSz;
    prev_ask_ = topAsk; prev_ask_sz_ = topAskSz;

    double bidTotal = 0.0;
    for (size_t i = 0; i < nb; ++i) {
        bidTotal += bidBuf[i].price.toDouble(tick_size_) * bidBuf[i].qty.value();
    }
    double askTotal = 0.0;
    for (size_t i = 0; i < na; ++i) {
        askTotal += askBuf[i].price.toDouble(tick_size_) * askBuf[i].qty.value();
    }
    double totalSum = bidTotal + askTotal;
    if (totalSum <= 0.0) return Signal::none();

    double imbalance_pct = (bidTotal / totalSum) * 100.0;
    double spread = topAsk - topBid;
    double mid = (topAsk + topBid) * 0.5;
    double spread_pct = mid > 0.0 ? spread / mid : 1.0;

    rolling_head_ = (rolling_head_ + 1) % params_.rolling_window;
    rolling_[rolling_head_] = imbalance_pct;
    if (rolling_count_ < params_.rolling_window) ++rolling_count_;

    bool trending_up = false;
    if (rolling_count_ > 1) {
        size_t prev = (rolling_head_ + params_.rolling_window - 1) % params_.rolling_window;
        trending_up = rolling_[rolling_head_] > rolling_[prev];
    }

    if (skip_next_signal_) {
        skip_next_signal_ = false;
        return Signal::none();
    }

    bool history_reversed_buy = false;
    bool history_reversed_sell = false;
    if (rolling_count_ == params_.rolling_window) {
        size_t oldest = (rolling_head_ + 1) % params_.rolling_window;
        history_reversed_buy = rolling_[rolling_head_] < rolling_[oldest];
        history_reversed_sell = rolling_[rolling_head_] > rolling_[oldest];
    }

    double secsSinceLastBuy = static_cast<double>((now - last_buy_ns_).count()) / 1e9;
    double secsSinceLastSell = static_cast<double>((now - last_sell_ns_).count()) / 1e9;

    if (cash_ > 0.0
        && imbalance_pct > params_.buy_imbalance_pct
        && trending_up
        && spread_pct < params_.buy_spread_pct
        && secsSinceLastBuy > params_.buy_cooldown_s) {
        OrderSizer::BuyInputs in{
            cash_, imbalance_pct, spread, mid,
            params_.buy_max_fraction, params_.rolling_window,
            history_reversed_buy
        };
        Signal s = OrderSizer::computeBuy(book, tick_size_, in);
        if (s.isActionable()) {
            s.ts = now;
            last_buy_ns_ = now;
            return s;
        }
    }

    if (position_ > 0.0
        && imbalance_pct < params_.sell_imbalance_pct
        && !trending_up
        && spread_pct < params_.sell_spread_pct
        && secsSinceLastSell > params_.sell_cooldown_s) {
        OrderSizer::SellInputs in{
            position_, imbalance_pct, spread, mid,
            params_.sell_max_fraction, params_.rolling_window,
            history_reversed_sell
        };
        Signal s = OrderSizer::computeSell(book, tick_size_, in);
        if (s.isActionable()) {
            s.ts = now;
            last_sell_ns_ = now;
            return s;
        }
    }

    return Signal::none();
}

template Signal ImbalanceStrategy::onMarketUpdate<4096>(const BoundedBook<4096>&, Timestamp) noexcept;

} // namespace trading
