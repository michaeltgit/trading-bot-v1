#include <catch2/catch_test_macros.hpp>

#include "trading/util/spsc_queue.hpp"

#include <atomic>
#include <thread>
#include <vector>

using namespace trading;

TEST_CASE("SPSCQueue basic push/pop", "[spsc]") {
    SPSCQueue<int, 16> q;
    REQUIRE(q.try_push(1));
    REQUIRE(q.try_push(2));
    REQUIRE(q.size() == 2);

    int v = 0;
    REQUIRE(q.try_pop(v));
    REQUIRE(v == 1);
    REQUIRE(q.try_pop(v));
    REQUIRE(v == 2);
    REQUIRE_FALSE(q.try_pop(v));
}

TEST_CASE("SPSCQueue rejects push when full", "[spsc]") {
    SPSCQueue<int, 4> q;
    REQUIRE(q.try_push(1));
    REQUIRE(q.try_push(2));
    REQUIRE(q.try_push(3));
    REQUIRE(q.try_push(4));
    REQUIRE_FALSE(q.try_push(5));
}

TEST_CASE("SPSCQueue wraps around the ring", "[spsc]") {
    SPSCQueue<int, 4> q;
    for (int i = 0; i < 100; ++i) {
        REQUIRE(q.try_push(i));
        int out;
        REQUIRE(q.try_pop(out));
        REQUIRE(out == i);
    }
}

TEST_CASE("SPSCQueue 1P/1C concurrent hammer", "[spsc][concurrent]") {
    constexpr int N = 1'000'000;
    SPSCQueue<int, 1024> q;

    std::thread producer([&] {
        for (int i = 0; i < N; ++i) {
            while (!q.try_push(i)) {
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer;
    std::atomic<int64_t> sum{0};
    int64_t expected = static_cast<int64_t>(N) * (N - 1) / 2;
    consumer = std::thread([&] {
        int popped = 0;
        int64_t local_sum = 0;
        int out = 0;
        while (popped < N) {
            if (q.try_pop(out)) {
                local_sum += out;
                ++popped;
            } else {
                std::this_thread::yield();
            }
        }
        sum = local_sum;
    });

    producer.join();
    consumer.join();
    REQUIRE(sum.load() == expected);
}
