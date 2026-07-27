#pragma once

#include "trading/core/types.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace trading {

template <size_t Capacity = 4096>
class BoundedBook {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

    // One bit per slot, plus a summary word holding one bit per bitmap word, so
    // the best level is two zero-count instructions rather than a ladder scan.
    static constexpr size_t WORD_BITS = 64;
    static constexpr size_t NUM_WORDS = Capacity / WORD_BITS;
    static_assert(Capacity >= WORD_BITS, "Capacity must cover at least one bitmap word");
    static_assert(NUM_WORDS <= WORD_BITS, "one summary word indexes at most 64 bitmap words");

    using Bitmap = std::array<uint64_t, NUM_WORDS>;

public:
    static constexpr size_t CENTER = Capacity / 2;
    static constexpr int64_t RECENTER_MARGIN = static_cast<int64_t>(Capacity / 8);

    void clear() noexcept {
        bids_.fill(0.0);
        asks_.fill(0.0);
        bid_bits_.fill(0);
        ask_bits_.fill(0);
        bid_summary_ = 0;
        ask_summary_ = 0;
        best_bid_idx_ = -1;
        best_ask_idx_ = static_cast<int32_t>(Capacity);
        have_ref_ = false;
    }

    // Anchors the window before the first level lands. Only takes effect on a
    // cleared book; a later call would reinterpret slots already written.
    void seedReference(Price price) noexcept {
        if (have_ref_) return;
        ref_tick_ = price.ticks();
        have_ref_ = true;
    }

    void update(Side side, Price price, Qty qty) noexcept {
        if (!have_ref_) {
            ref_tick_ = price.ticks();
            have_ref_ = true;
        }

        int64_t idx = static_cast<int64_t>(CENTER) + (price.ticks() - ref_tick_);
        // Outside the tracked window: deep book or stale. Ignore it rather than
        // recentering onto an outlier, which would drop the real near-touch levels.
        if (idx < 0 || idx >= static_cast<int64_t>(Capacity)) return;

        const auto slot = static_cast<size_t>(idx);
        const auto slot_i32 = static_cast<int32_t>(idx);
        // Same predicate depth() filters on, so index and ladder cannot disagree.
        const bool live = qty.value() > 0.0;

        if (side == Side::Bid) {
            bids_[slot] = qty.value();
            if (live) {
                setBit(bid_bits_, bid_summary_, slot);
                if (slot_i32 > best_bid_idx_) best_bid_idx_ = slot_i32;
            } else {
                clearBit(bid_bits_, bid_summary_, slot);
                if (slot_i32 == best_bid_idx_) best_bid_idx_ = highestSet(bid_bits_, bid_summary_);
            }
        } else {
            asks_[slot] = qty.value();
            if (live) {
                setBit(ask_bits_, ask_summary_, slot);
                if (slot_i32 < best_ask_idx_) best_ask_idx_ = slot_i32;
            } else {
                clearBit(ask_bits_, ask_summary_, slot);
                if (slot_i32 == best_ask_idx_) best_ask_idx_ = lowestSet(ask_bits_, ask_summary_);
            }
        }

        recenterIfNearEdge();
    }

    std::optional<PriceLevel> topOfBook(Side side) const noexcept {
        if (side == Side::Bid) {
            if (best_bid_idx_ < 0) return std::nullopt;
            return PriceLevel{
                Price::fromTicks(ref_tick_ + (best_bid_idx_ - static_cast<int32_t>(CENTER))),
                Qty{bids_[static_cast<size_t>(best_bid_idx_)]}};
        } else {
            if (best_ask_idx_ >= static_cast<int32_t>(Capacity)) return std::nullopt;
            return PriceLevel{
                Price::fromTicks(ref_tick_ + (best_ask_idx_ - static_cast<int32_t>(CENTER))),
                Qty{asks_[static_cast<size_t>(best_ask_idx_)]}};
        }
    }

    size_t depth(Side side, PriceLevel* out, size_t n) const noexcept {
        size_t filled = 0;
        if (side == Side::Bid) {
            for (int32_t i = best_bid_idx_; i >= 0 && filled < n; --i) {
                if (bids_[static_cast<size_t>(i)] > 0.0) {
                    out[filled++] =
                        PriceLevel{Price::fromTicks(ref_tick_ + (i - static_cast<int32_t>(CENTER))),
                                   Qty{bids_[static_cast<size_t>(i)]}};
                }
            }
        } else {
            for (int32_t i = best_ask_idx_; i < static_cast<int32_t>(Capacity) && filled < n; ++i) {
                if (asks_[static_cast<size_t>(i)] > 0.0) {
                    out[filled++] =
                        PriceLevel{Price::fromTicks(ref_tick_ + (i - static_cast<int32_t>(CENTER))),
                                   Qty{asks_[static_cast<size_t>(i)]}};
                }
            }
        }
        return filled;
    }

    uint64_t rebaseCount() const noexcept { return rebase_count_; }

private:
    static constexpr uint64_t bitAt(size_t b) noexcept { return uint64_t{1} << b; }

    static void setBit(Bitmap& bits, uint64_t& summary, size_t idx) noexcept {
        bits[idx / WORD_BITS] |= bitAt(idx % WORD_BITS);
        summary |= bitAt(idx / WORD_BITS);
    }

    static void clearBit(Bitmap& bits, uint64_t& summary, size_t idx) noexcept {
        const size_t w = idx / WORD_BITS;
        bits[w] &= ~bitAt(idx % WORD_BITS);
        if (bits[w] == 0) summary &= ~bitAt(w);
    }

    static int32_t highestSet(const Bitmap& bits, uint64_t summary) noexcept {
        if (summary == 0) return -1;
        const auto w = static_cast<size_t>(WORD_BITS - 1 - __builtin_clzll(summary));
        const auto b = static_cast<size_t>(WORD_BITS - 1 - __builtin_clzll(bits[w]));
        return static_cast<int32_t>(w * WORD_BITS + b);
    }

    static int32_t lowestSet(const Bitmap& bits, uint64_t summary) noexcept {
        if (summary == 0) return static_cast<int32_t>(Capacity);
        const auto w = static_cast<size_t>(__builtin_ctzll(summary));
        const auto b = static_cast<size_t>(__builtin_ctzll(bits[w]));
        return static_cast<int32_t>(w * WORD_BITS + b);
    }

    // Recenter the window on the current touch once it drifts near an edge, so the
    // book follows genuine price movement without ever centering on a far outlier.
    void recenterIfNearEdge() noexcept {
        bool haveBid = best_bid_idx_ >= 0;
        bool haveAsk = best_ask_idx_ < static_cast<int32_t>(Capacity);
        if (!haveBid && !haveAsk) return;

        int64_t lo = haveBid ? best_bid_idx_ : best_ask_idx_;
        int64_t hi = haveAsk ? best_ask_idx_ : best_bid_idx_;
        if (haveBid && haveAsk) {
            lo = std::min<int64_t>(best_bid_idx_, best_ask_idx_);
            hi = std::max<int64_t>(best_bid_idx_, best_ask_idx_);
        }
        if (lo >= RECENTER_MARGIN && hi < static_cast<int64_t>(Capacity) - RECENTER_MARGIN) {
            return;
        }

        int64_t newRef = ref_tick_ + ((lo + hi) / 2 - static_cast<int64_t>(CENTER));
        if (newRef != ref_tick_) rebase(newRef);
    }

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
        rebuildIndex();
        ++rebase_count_;
    }

    // Shifting the ladder moves every slot, so the index is rebuilt wholesale.
    void rebuildIndex() noexcept {
        bid_bits_.fill(0);
        ask_bits_.fill(0);
        bid_summary_ = 0;
        ask_summary_ = 0;
        for (size_t i = 0; i < Capacity; ++i) {
            if (bids_[i] > 0.0) setBit(bid_bits_, bid_summary_, i);
            if (asks_[i] > 0.0) setBit(ask_bits_, ask_summary_, i);
        }
        best_bid_idx_ = highestSet(bid_bits_, bid_summary_);
        best_ask_idx_ = lowestSet(ask_bits_, ask_summary_);
    }

    int64_t ref_tick_ = 0;
    bool have_ref_ = false;
    std::array<double, Capacity> bids_{};
    std::array<double, Capacity> asks_{};
    Bitmap bid_bits_{};
    Bitmap ask_bits_{};
    uint64_t bid_summary_ = 0;
    uint64_t ask_summary_ = 0;
    int32_t best_bid_idx_ = -1;
    int32_t best_ask_idx_ = static_cast<int32_t>(Capacity);
    uint64_t rebase_count_ = 0;
};

}  // namespace trading
