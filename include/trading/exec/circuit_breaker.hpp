#pragma once

#include <atomic>
#include <cstdint>

namespace trading {

class CircuitBreaker {
public:
    CircuitBreaker(uint64_t threshold, uint64_t windowMessages) noexcept
        : threshold_(threshold), windowMessages_(windowMessages) {}

    void recordMessage() noexcept {
        if (windowMessages_ == 0) return;
        uint64_t n = msgs_.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n % windowMessages_ == 0) {
            errors_.store(0, std::memory_order_relaxed);
            tripped_.store(false, std::memory_order_release);
        }
    }

    void recordError() noexcept {
        uint64_t e = errors_.fetch_add(1, std::memory_order_relaxed) + 1;
        if (e >= threshold_) {
            tripped_.store(true, std::memory_order_release);
        }
    }

    bool isTripped() const noexcept { return tripped_.load(std::memory_order_acquire); }

    void reset() noexcept {
        tripped_.store(false, std::memory_order_release);
        errors_.store(0, std::memory_order_relaxed);
    }

private:
    uint64_t threshold_;
    uint64_t windowMessages_;
    std::atomic<uint64_t> msgs_{0};
    std::atomic<uint64_t> errors_{0};
    std::atomic<bool> tripped_{false};
};

} // namespace trading
