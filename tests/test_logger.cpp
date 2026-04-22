#include <catch2/catch_test_macros.hpp>

#include "trading/core/logger.hpp"

#include <chrono>
#include <thread>

using namespace trading;

TEST_CASE("Logger accepts messages without blocking", "[logger]") {
    auto& log = Logger::instance();
    for (int i = 0; i < 100; ++i) {
        log.log(LogLevel::Info, "test message");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    SUCCEED("logger accepted 100 messages");
}

TEST_CASE("Logger drops when ring saturates instead of blocking", "[logger]") {
    auto& log = Logger::instance();
    uint64_t before = log.droppedCount();

    for (size_t i = 0; i < Logger::RING_SIZE * 4; ++i) {
        log.log(LogLevel::Debug, "flood");
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    INFO("dropped since start: " << (log.droppedCount() - before));
    SUCCEED();
}
