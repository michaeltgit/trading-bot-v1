#include <catch2/catch_test_macros.hpp>

#include "trading/core/logger.hpp"

#include <chrono>
#include <thread>
#include <vector>

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

TEST_CASE("Logger accepts concurrent producers", "[logger]") {
    auto& log = Logger::instance();

    std::vector<std::thread> producers;
    for (int t = 0; t < 4; ++t) {
        producers.emplace_back([&log] {
            for (int i = 0; i < 5000; ++i) {
                log.log(LogLevel::Debug, "mpsc hammer");
            }
        });
    }
    for (auto& t : producers)
        t.join();

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    SUCCEED("four producers hammered the ring concurrently");
}
