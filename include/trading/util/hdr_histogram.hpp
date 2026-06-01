#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>

namespace trading {

// Each power-of-two octave is split into SUB_COUNT linear sub-buckets, giving
// constant ~3% relative error. Counters are relaxed atomics: one writer can
// record while a reader dumps.
class HdrHistogram {
public:
    static constexpr int SUB_BITS = 5;                       // 32 sub-buckets per octave
    static constexpr size_t SUB_COUNT = size_t{1} << SUB_BITS;
    static constexpr size_t NUM_CELLS = 64 * SUB_COUNT;      // upper bound on index range

    HdrHistogram() = default;

    void recordNs(int64_t ns) noexcept {
        if (ns < 0) ns = 0;
        auto v = static_cast<uint64_t>(ns);
        counts_[indexFor(v)].fetch_add(1, std::memory_order_relaxed);
        count_.fetch_add(1, std::memory_order_relaxed);
        sum_.fetch_add(v, std::memory_order_relaxed);
        if (v > max_.load(std::memory_order_relaxed)) {
            max_.store(v, std::memory_order_relaxed);
        }
    }

    uint64_t count() const noexcept { return count_.load(std::memory_order_relaxed); }
    int64_t max() const noexcept { return static_cast<int64_t>(max_.load(std::memory_order_relaxed)); }
    double mean() const noexcept {
        uint64_t n = count();
        return n ? static_cast<double>(sum_.load(std::memory_order_relaxed)) / static_cast<double>(n) : 0.0;
    }

    int64_t percentileNs(double pct) const noexcept {
        uint64_t n = count();
        if (n == 0) return 0;
        auto target = static_cast<uint64_t>(pct / 100.0 * static_cast<double>(n));
        if (target == 0) target = 1;
        uint64_t cum = 0;
        int64_t mx = max();
        for (size_t i = 0; i < NUM_CELLS; ++i) {
            cum += counts_[i].load(std::memory_order_relaxed);
            if (cum >= target) {
                int64_t v = valueAt(i);
                return v < mx ? v : mx;
            }
        }
        return mx;
    }

    void dump(std::ostream& os, const std::string& name) const {
        os << name
           << " n=" << count()
           << " p50=" << percentileNs(50)
           << "ns p95=" << percentileNs(95)
           << "ns p99=" << percentileNs(99)
           << "ns p99.9=" << percentileNs(99.9)
           << "ns max=" << max() << "ns";
    }

private:
    static size_t indexFor(uint64_t v) noexcept {
        if (v < SUB_COUNT) return static_cast<size_t>(v);
        int exp = 63 - __builtin_clzll(v);          // position of leading set bit (>= SUB_BITS)
        int octave = exp - SUB_BITS;                 // >= 0
        size_t sub = static_cast<size_t>(v >> octave) - SUB_COUNT;  // in [0, SUB_COUNT)
        size_t idx = SUB_COUNT * static_cast<size_t>(octave + 1) + sub;
        return idx < NUM_CELLS ? idx : NUM_CELLS - 1;
    }

    static int64_t valueAt(size_t idx) noexcept {
        if (idx < SUB_COUNT) return static_cast<int64_t>(idx);
        size_t rel = idx - SUB_COUNT;
        size_t octave = rel / SUB_COUNT;
        size_t sub = rel % SUB_COUNT;
        uint64_t lowerEdge = (SUB_COUNT + sub) << octave;
        uint64_t halfWidth = (uint64_t{1} << octave) >> 1;
        return static_cast<int64_t>(lowerEdge + halfWidth);
    }

    std::array<std::atomic<uint64_t>, NUM_CELLS> counts_{};
    std::atomic<uint64_t> count_{0};
    std::atomic<uint64_t> sum_{0};
    std::atomic<uint64_t> max_{0};
};

} // namespace trading
