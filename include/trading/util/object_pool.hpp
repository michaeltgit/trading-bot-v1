#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

namespace trading {

template <typename T, size_t Capacity>
class ObjectPool {
    static_assert(Capacity > 0);

public:
    ObjectPool() noexcept {
        for (size_t i = 0; i < Capacity; ++i) {
            freelist_[i] = static_cast<uint32_t>(i);
        }
        free_top_ = Capacity;
    }

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    template <typename... Args>
    T* acquire(Args&&... args) noexcept {
        if (free_top_ == 0) {
            return nullptr;
        }
        uint32_t idx = freelist_[--free_top_];
        T* p = reinterpret_cast<T*>(&storage_[idx]);
        return new (p) T(std::forward<Args>(args)...);
    }

    void release(T* p) noexcept {
        if (!p) return;
        uint32_t idx = static_cast<uint32_t>(
            reinterpret_cast<Storage*>(p) - &storage_[0]);
        p->~T();
        freelist_[free_top_++] = idx;
    }

    size_t available() const noexcept { return free_top_; }
    static constexpr size_t capacity() noexcept { return Capacity; }

private:
    using Storage = std::aligned_storage_t<sizeof(T), alignof(T)>;
    std::array<Storage, Capacity> storage_{};
    std::array<uint32_t, Capacity> freelist_{};
    uint32_t free_top_ = 0;
};

} // namespace trading
