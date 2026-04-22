#pragma once

#include "trading/core/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace trading {

template <size_t Capacity = 4096>
class BoundedBook {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    static constexpr size_t CENTER = Capacity / 2;
    static constexpr int64_t REBASE_THRESHOLD = static_cast<int64_t>(Capacity / 4);

    explicit BoundedBook(double tickSize) noexcept : tick_(tickSize) {}

    double tickSize() const noexcept { return tick_; }
    bool hasReference() const noexcept { return have_ref_; }
    int64_t referenceTick() const noexcept { return ref_tick_; }

    void clear() noexcept {
        bids_.fill(0.0);
        asks_.fill(0.0);
        best_bid_idx_ = -1;
        best_ask_idx_ = static_cast<int32_t>(Capacity);
        have_ref_ = false;
    }

    void update(Side side, Price price, Qty qty) noexcept {
        if (!have_ref_) {
            ref_tick_ = price.ticks();
            have_ref_ = true;
        }

        int64_t offset = price.ticks() - ref_tick_;
        if (offset <= -REBASE_THRESHOLD || offset >= REBASE_THRESHOLD) {
            rebase(price.ticks());
            offset = 0;
        }
        int64_t idx = static_cast<int64_t>(CENTER) + offset;
        if (idx < 0 || idx >= static_cast<int64_t>(Capacity)) return;

        auto& book = (side == Side::Bid) ? bids_ : asks_;
        book[static_cast<size_t>(idx)] = qty.value();

        if (side == Side::Bid) {
            if (qty.isZero() && static_cast<int32_t>(idx) == best_bid_idx_) {
                recomputeBestBid();
            } else if (!qty.isZero() && static_cast<int32_t>(idx) > best_bid_idx_) {
                best_bid_idx_ = static_cast<int32_t>(idx);
            }
        } else {
            if (qty.isZero() && static_cast<int32_t>(idx) == best_ask_idx_) {
                recomputeBestAsk();
            } else if (!qty.isZero() && static_cast<int32_t>(idx) < best_ask_idx_) {
                best_ask_idx_ = static_cast<int32_t>(idx);
            }
        }
    }

    std::optional<PriceLevel> topOfBook(Side side) const noexcept {
        if (side == Side::Bid) {
            if (best_bid_idx_ < 0) return std::nullopt;
            return PriceLevel{
                Price::fromTicks(ref_tick_ + (best_bid_idx_ - static_cast<int32_t>(CENTER))),
                Qty{bids_[static_cast<size_t>(best_bid_idx_)]}
            };
        } else {
            if (best_ask_idx_ >= static_cast<int32_t>(Capacity)) return std::nullopt;
            return PriceLevel{
                Price::fromTicks(ref_tick_ + (best_ask_idx_ - static_cast<int32_t>(CENTER))),
                Qty{asks_[static_cast<size_t>(best_ask_idx_)]}
            };
        }
    }

    size_t depth(Side side, PriceLevel* out, size_t n) const noexcept {
        size_t filled = 0;
        if (side == Side::Bid) {
            for (int32_t i = best_bid_idx_; i >= 0 && filled < n; --i) {
                if (bids_[static_cast<size_t>(i)] > 0.0) {
                    out[filled++] = PriceLevel{
                        Price::fromTicks(ref_tick_ + (i - static_cast<int32_t>(CENTER))),
                        Qty{bids_[static_cast<size_t>(i)]}
                    };
                }
            }
        } else {
            for (int32_t i = best_ask_idx_;
                 i < static_cast<int32_t>(Capacity) && filled < n; ++i) {
                if (asks_[static_cast<size_t>(i)] > 0.0) {
                    out[filled++] = PriceLevel{
                        Price::fromTicks(ref_tick_ + (i - static_cast<int32_t>(CENTER))),
                        Qty{asks_[static_cast<size_t>(i)]}
                    };
                }
            }
        }
        return filled;
    }

    uint64_t rebaseCount() const noexcept { return rebase_count_; }

private:
    void rebase(int64_t newRef) noexcept {
        std::array<double, Capacity> nb{};
        std::array<double, Capacity> na{};
        int64_t shift = newRef - ref_tick_;
        for (size_t i = 0; i < Capacity; ++i) {
            int64_t src = static_cast<int64_t>(i) + shift;
            if (src >= 0 && src < static_cast<int64_t>(Capacity)) {
                nb[i] = bids_[static_cast<size_t>(src)];
                na[i] = asks_[static_cast<size_t>(src)];
            }
        }
        bids_ = nb;
        asks_ = na;
        ref_tick_ = newRef;
        recomputeBestBid();
        recomputeBestAsk();
        ++rebase_count_;
    }

    void recomputeBestBid() noexcept {
        for (int32_t i = static_cast<int32_t>(Capacity) - 1; i >= 0; --i) {
            if (bids_[static_cast<size_t>(i)] > 0.0) {
                best_bid_idx_ = i;
                return;
            }
        }
        best_bid_idx_ = -1;
    }

    void recomputeBestAsk() noexcept {
        for (size_t i = 0; i < Capacity; ++i) {
            if (asks_[i] > 0.0) {
                best_ask_idx_ = static_cast<int32_t>(i);
                return;
            }
        }
        best_ask_idx_ = static_cast<int32_t>(Capacity);
    }

    double tick_;
    int64_t ref_tick_ = 0;
    bool have_ref_ = false;
    std::array<double, Capacity> bids_{};
    std::array<double, Capacity> asks_{};
    int32_t best_bid_idx_ = -1;
    int32_t best_ask_idx_ = static_cast<int32_t>(Capacity);
    uint64_t rebase_count_ = 0;
};

} // namespace trading
