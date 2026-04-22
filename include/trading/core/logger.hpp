#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <thread>

namespace trading {

enum class LogLevel : uint8_t {
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
};

constexpr std::string_view toString(LogLevel lvl) noexcept {
    switch (lvl) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "?";
}

class Logger {
public:
    static constexpr size_t RING_SIZE = 4096;
    static constexpr size_t MAX_LINE = 512;

    struct Record {
        LogLevel level;
        int64_t wallNs;
        char text[MAX_LINE];
    };

    static Logger& instance() noexcept;

    void log(LogLevel level, std::string_view line) noexcept;

    uint64_t droppedCount() const noexcept { return dropped_.load(std::memory_order_relaxed); }

    void stop() noexcept;

private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void drainLoop() noexcept;

    std::array<Record, RING_SIZE> ring_;
    alignas(64) std::atomic<uint64_t> head_{0};
    alignas(64) std::atomic<uint64_t> tail_{0};
    alignas(64) std::atomic<uint64_t> dropped_{0};
    std::atomic<bool> running_{true};
    std::thread drainer_;
};

} // namespace trading

#define TRADING_LOG(level, msg) ::trading::Logger::instance().log((level), (msg))
#define TRADING_LOG_DEBUG(msg) TRADING_LOG(::trading::LogLevel::Debug, msg)
#define TRADING_LOG_INFO(msg)  TRADING_LOG(::trading::LogLevel::Info, msg)
#define TRADING_LOG_WARN(msg)  TRADING_LOG(::trading::LogLevel::Warn, msg)
#define TRADING_LOG_ERROR(msg) TRADING_LOG(::trading::LogLevel::Error, msg)
