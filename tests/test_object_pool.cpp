#include <catch2/catch_test_macros.hpp>

#include "trading/util/object_pool.hpp"

#include <string>
#include <vector>

using namespace trading;

struct Thing {
    int a;
    std::string s;
    Thing(int x, std::string y) : a(x), s(std::move(y)) {}
};

TEST_CASE("ObjectPool acquire/release round-trips", "[pool]") {
    ObjectPool<Thing, 8> pool;
    REQUIRE(pool.available() == 8);

    auto* p = pool.acquire(42, "hello");
    REQUIRE(p != nullptr);
    REQUIRE(p->a == 42);
    REQUIRE(p->s == "hello");
    REQUIRE(pool.available() == 7);

    pool.release(p);
    REQUIRE(pool.available() == 8);
}

TEST_CASE("ObjectPool returns nullptr on exhaustion", "[pool]") {
    ObjectPool<int, 4> pool;
    std::vector<int*> acquired;
    for (int i = 0; i < 4; ++i) {
        auto* p = pool.acquire(i);
        REQUIRE(p != nullptr);
        acquired.push_back(p);
    }
    REQUIRE(pool.acquire(99) == nullptr);

    for (auto* p : acquired) pool.release(p);
    REQUIRE(pool.available() == 4);
}

TEST_CASE("ObjectPool reuses slots after release", "[pool]") {
    ObjectPool<int, 2> pool;
    auto* a = pool.acquire(1);
    pool.release(a);
    auto* b = pool.acquire(2);
    REQUIRE(b == a); // LIFO freelist: same slot reused
    REQUIRE(*b == 2);
    pool.release(b);
}

TEST_CASE("ObjectPool handles null release safely", "[pool]") {
    ObjectPool<int, 2> pool;
    pool.release(nullptr);
    REQUIRE(pool.available() == 2);
}
