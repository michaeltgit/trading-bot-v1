#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

namespace trading {

template <typename T, size_t Capacity>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static_assert(Capacity >= 2, "Capacity must be >= 2");
    static_assert(std::is_trivially_copyable_v<T> || std::is_move_constructible_v<T>,
                  "T must be trivially copyable or move-constructible");

public:
    SPSCQueue() : slots_(std::make_unique<T[]>(Capacity)) {}
    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    bool try_push(const T& value) noexcept {
        const uint64_t head = head_.load(std::memory_order_relaxed);
        const uint64_t tail = tail_.load(std::memory_order_acquire);
        if (head - tail >= Capacity) {
            return false;
        }
        slots_[head & (Capacity - 1)] = value;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    bool try_push(T&& value) noexcept {
        const uint64_t head = head_.load(std::memory_order_relaxed);
        const uint64_t tail = tail_.load(std::memory_order_acquire);
        if (head - tail >= Capacity) {
            return false;
        }
        slots_[head & (Capacity - 1)] = std::move(value);
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    bool try_pop(T& out) noexcept {
        const uint64_t tail = tail_.load(std::memory_order_relaxed);
        const uint64_t head = head_.load(std::memory_order_acquire);
        if (tail == head) {
            return false;
        }
        out = std::move(slots_[tail & (Capacity - 1)]);
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    size_t size() const noexcept {
        return static_cast<size_t>(head_.load(std::memory_order_acquire) -
                                   tail_.load(std::memory_order_acquire));
    }

    static constexpr size_t capacity() noexcept { return Capacity; }

private:
    alignas(64) std::atomic<uint64_t> head_{0};
    alignas(64) std::atomic<uint64_t> tail_{0};
    std::unique_ptr<T[]> slots_;
};

}  // namespace trading
