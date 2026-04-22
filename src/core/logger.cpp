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
    uint64_t tail = tail_.load(std::memory_order_acquire);
    if (head - tail >= RING_SIZE) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    Record& r = ring_[head & (RING_SIZE - 1)];
    r.level = level;
    r.wallNs = Clock::wallNs();
    size_t n = std::min(line.size(), MAX_LINE - 1);
    std::memcpy(r.text, line.data(), n);
    r.text[n] = '\0';
    head_.store(head + 1, std::memory_order_release);
}

void Logger::drainLoop() noexcept {
    while (running_.load(std::memory_order_relaxed) ||
           tail_.load(std::memory_order_relaxed) != head_.load(std::memory_order_relaxed)) {
        uint64_t head = head_.load(std::memory_order_acquire);
        uint64_t tail = tail_.load(std::memory_order_relaxed);

        if (tail == head) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        while (tail < head) {
            const Record& r = ring_[tail & (RING_SIZE - 1)];

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

            std::fprintf(stderr, "[%s.%06lld] [%-5s] %s\n",
                         ts,
                         static_cast<long long>(nsec / 1000),
                         toString(r.level).data(),
                         r.text);
            ++tail;
        }
        tail_.store(tail, std::memory_order_release);
    }
    std::fflush(stderr);
}

} // namespace trading
