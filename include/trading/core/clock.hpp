#pragma once

#include "trading/core/types.hpp"

#include <chrono>

namespace trading {

class Clock {
public:
    static Timestamp now() noexcept {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch());
    }

    static int64_t wallNs() noexcept {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
    }
};

} // namespace trading
