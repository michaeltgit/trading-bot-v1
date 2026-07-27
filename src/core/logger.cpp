#include "trading/core/logger.hpp"

#include "trading/core/clock.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace trading {

Logger& Logger::instance() noexcept {
    static Logger logger;
    return logger;
}

Logger::Logger() {
    drainer_ = std::thread([this] { drainLoop(); });
}

Logger::~Logger() {
    stop();
}

void Logger::stop() noexcept {
    if (running_.exchange(false)) {
        if (drainer_.joinable()) {
            drainer_.join();
        }
    }
}

void Logger::log(LogLevel level, std::string_view line) noexcept {
    uint64_t head = head_.load(std::memory_order_relaxed);
    for (;;) {
        if (head - tail_.load(std::memory_order_acquire) >= RING_SIZE) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (head_.compare_exchange_weak(head, head + 1, std::memory_order_relaxed)) {
            break;
        }
    }
    Record& r = ring_[head & (RING_SIZE - 1)];
    r.level = level;
    r.wallNs = Clock::wallNs();
    size_t n = std::min(line.size(), MAX_LINE - 1);
    std::memcpy(r.text, line.data(), n);
    r.text[n] = '\0';
    r.ready.store(head + 1, std::memory_order_release);
}

void Logger::drainLoop() noexcept {
    for (;;) {
        uint64_t head = head_.load(std::memory_order_acquire);
        uint64_t tail = tail_.load(std::memory_order_relaxed);

        if (tail == head) {
            if (!running_.load(std::memory_order_relaxed)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        bool progressed = false;
        while (tail < head) {
            const Record& r = ring_[tail & (RING_SIZE - 1)];
            if (r.ready.load(std::memory_order_acquire) != tail + 1) break;  // still being written

            time_t sec = static_cast<time_t>(r.wallNs / 1'000'000'000LL);
            int64_t nsec = r.wallNs % 1'000'000'000LL;
            struct tm tm_buf;
#ifdef _WIN32
            localtime_s(&tm_buf, &sec);
#else
            localtime_r(&sec, &tm_buf);
#endif
            char ts[32];
            std::strftime(ts, sizeof(ts), "%H:%M:%S", &tm_buf);

            std::fprintf(stderr, "[%s.%06lld] [%-5s] %s\n", ts, static_cast<long long>(nsec / 1000),
                         toString(r.level).data(), r.text);
            ++tail;
            progressed = true;
        }
        tail_.store(tail, std::memory_order_release);
        if (!progressed) std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
    std::fflush(stderr);
}

}  // namespace trading
