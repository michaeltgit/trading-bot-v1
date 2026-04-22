#include <catch2/catch_test_macros.hpp>

#include "trading/util/hdr_histogram.hpp"

using namespace trading;

TEST_CASE("HdrHistogram empty reports zeros", "[hdr]") {
    HdrHistogram h;
    REQUIRE(h.count() == 0);
    REQUIRE(h.max() == 0);
    REQUIRE(h.percentileNs(50) == 0);
}

TEST_CASE("HdrHistogram tracks max and count", "[hdr]") {
    HdrHistogram h;
    h.recordNs(100);
    h.recordNs(200);
    h.recordNs(500);
    REQUIRE(h.count() == 3);
    REQUIRE(h.max() == 500);
    REQUIRE(h.mean() > 0);
}

TEST_CASE("HdrHistogram percentiles are monotonic", "[hdr]") {
    HdrHistogram h;
    for (int i = 1; i <= 10000; ++i) {
        h.recordNs(i);
    }
    int64_t p50 = h.percentileNs(50);
    int64_t p95 = h.percentileNs(95);
    int64_t p99 = h.percentileNs(99);
    REQUIRE(p50 <= p95);
    REQUIRE(p95 <= p99);
    REQUIRE(p99 <= h.max());
}

TEST_CASE("HdrHistogram handles zero/negative gracefully", "[hdr]") {
    HdrHistogram h;
    h.recordNs(0);
    h.recordNs(-5);
    REQUIRE(h.count() == 2);
}
