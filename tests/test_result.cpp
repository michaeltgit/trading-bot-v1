#include <catch2/catch_test_macros.hpp>

#include "trading/core/result.hpp"

#include <string>

using namespace trading;

TEST_CASE("Result<int> success path", "[result]") {
    Result<int> r{42};
    REQUIRE(r.ok());
    REQUIRE(r);
    REQUIRE(r.value() == 42);
    REQUIRE(r.error() == Error::Ok);
}

TEST_CASE("Result<int> error path", "[result]") {
    Result<int> r{Error::NetworkConnectFailed};
    REQUIRE_FALSE(r.ok());
    REQUIRE_FALSE(r);
    REQUIRE(r.error() == Error::NetworkConnectFailed);
}

TEST_CASE("Result<void> success", "[result]") {
    Result<void> r;
    REQUIRE(r.ok());
    REQUIRE(r.error() == Error::Ok);
}

TEST_CASE("Result<void> error", "[result]") {
    Result<void> r{Error::ConfigInvalid};
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error() == Error::ConfigInvalid);
}

TEST_CASE("Result<string> moves cleanly", "[result]") {
    Result<std::string> r{std::string{"hello"}};
    REQUIRE(r.ok());
    REQUIRE(r.value() == "hello");
    std::string taken = std::move(r).value();
    REQUIRE(taken == "hello");
}

TEST_CASE("Error::toString covers every code", "[result]") {
    REQUIRE(toString(Error::Ok) == "Ok");
    REQUIRE(toString(Error::SequenceGap) == "SequenceGap");
    REQUIRE(toString(Error::PoolExhausted) == "PoolExhausted");
}
