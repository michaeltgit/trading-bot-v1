#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>

namespace trading {

class HdrHistogram {
public:
    static constexpr size_t NUM_BUCKETS = 64;

    HdrHistogram() noexcept = default;

    void recordNs(int64_t ns) noexcept {
        if (ns <= 0) {
            ++counts_[0];
            ++count_;
            return;
        }
        int lz = __builtin_clzll(static_cast<uint64_t>(ns));
        size_t bucket = static_cast<size_t>(63 - lz) + 1;
        if (bucket >= NUM_BUCKETS) bucket = NUM_BUCKETS - 1;
        ++counts_[bucket];
        ++count_;
        if (ns > max_) max_ = ns;
        sum_ += static_cast<uint64_t>(ns);
    }

    uint64_t count() const noexcept { return count_; }
    int64_t max() const noexcept { return max_; }
    double mean() const noexcept { return count_ ? static_cast<double>(sum_) / count_ : 0.0; }

    int64_t percentileNs(double pct) const noexcept {
        if (count_ == 0) return 0;
        uint64_t target = static_cast<uint64_t>(pct / 100.0 * count_);
        uint64_t cum = 0;
        for (size_t i = 0; i < NUM_BUCKETS; ++i) {
            cum += counts_[i];
            if (cum >= target) {
                return 1LL << (i == 0 ? 0 : i - 1);
            }
        }
        return max_;
    }

    void dump(std::ostream& os, const std::string& name) const {
        os << name
           << " n=" << count_
           << " p50=" << percentileNs(50)
           << "ns p95=" << percentileNs(95)
           << "ns p99=" << percentileNs(99)
           << "ns p99.9=" << percentileNs(99.9)
           << "ns max=" << max_ << "ns";
    }

private:
    std::array<uint64_t, NUM_BUCKETS> counts_{};
    uint64_t count_ = 0;
    uint64_t sum_ = 0;
    int64_t max_ = 0;
};

} // namespace trading
