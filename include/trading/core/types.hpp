#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace trading {

class Price {
public:
    constexpr Price() noexcept = default;

    static Price fromTicks(int64_t ticks) noexcept {
        Price p;
        p.ticks_ = ticks;
        return p;
    }

    static Price fromDouble(double px, double tickSize) noexcept {
        Price p;
        p.ticks_ = static_cast<int64_t>(std::llround(px / tickSize));
        return p;
    }

    constexpr int64_t ticks() const noexcept { return ticks_; }

    double toDouble(double tickSize) const noexcept {
        return static_cast<double>(ticks_) * tickSize;
    }

    constexpr bool operator==(Price other) const noexcept { return ticks_ == other.ticks_; }
    constexpr bool operator!=(Price other) const noexcept { return ticks_ != other.ticks_; }
    constexpr bool operator<(Price other) const noexcept { return ticks_ < other.ticks_; }
    constexpr bool operator<=(Price other) const noexcept { return ticks_ <= other.ticks_; }
    constexpr bool operator>(Price other) const noexcept { return ticks_ > other.ticks_; }
    constexpr bool operator>=(Price other) const noexcept { return ticks_ >= other.ticks_; }

private:
    int64_t ticks_ = 0;
};

class Qty {
public:
    constexpr Qty() noexcept = default;
    explicit constexpr Qty(double v) noexcept : v_(v) {}

    constexpr double value() const noexcept { return v_; }
    constexpr bool isZero() const noexcept { return v_ == 0.0; }

    Qty operator+(Qty o) const noexcept { return Qty{v_ + o.v_}; }
    Qty operator-(Qty o) const noexcept { return Qty{v_ - o.v_}; }
    Qty& operator+=(Qty o) noexcept {
        v_ += o.v_;
        return *this;
    }
    Qty& operator-=(Qty o) noexcept {
        v_ -= o.v_;
        return *this;
    }

    constexpr bool operator==(Qty o) const noexcept { return v_ == o.v_; }
    constexpr bool operator<(Qty o) const noexcept { return v_ < o.v_; }
    constexpr bool operator<=(Qty o) const noexcept { return v_ <= o.v_; }
    constexpr bool operator>(Qty o) const noexcept { return v_ > o.v_; }
    constexpr bool operator>=(Qty o) const noexcept { return v_ >= o.v_; }

private:
    double v_ = 0.0;
};

enum class Side : uint8_t {
    Bid = 0,
    Ask = 1,
};

constexpr Side opposite(Side s) noexcept {
    return s == Side::Bid ? Side::Ask : Side::Bid;
}

using SymbolId = uint16_t;
inline constexpr SymbolId INVALID_SYMBOL_ID = static_cast<SymbolId>(-1);
inline constexpr size_t MAX_SYMBOLS = 64;

using OrderId = uint64_t;

using Timestamp = std::chrono::nanoseconds;

struct PriceLevel {
    Price price;
    Qty qty;
};

// Deepest ladder walked when sizing or simulating an order. The fill simulator
// must walk at least as deep as the sizer, or a deep order would spuriously
// partial-fill against the very book it was sized from.
inline constexpr size_t MAX_BOOK_WALK = 32;

}  // namespace trading
