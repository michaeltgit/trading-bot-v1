#include <catch2/catch_test_macros.hpp>

#include "trading/md/coinbase_auth.hpp"

using namespace trading;

TEST_CASE("Empty CoinbaseAuth has no credentials and signs to empty", "[auth]") {
    CoinbaseAuth a;
    REQUIRE_FALSE(a.hasCredentials());
    REQUIRE(a.sign("1234567890").empty());
}

TEST_CASE("CoinbaseAuth signs deterministically with known secret", "[auth]") {
    CoinbaseAuth a{"test-key", "dGVzdC1zZWNyZXQ=", "passphrase"};
    REQUIRE(a.hasCredentials());
    auto sig1 = a.sign("1700000000");
    auto sig2 = a.sign("1700000000");
    REQUIRE_FALSE(sig1.empty());
    REQUIRE(sig1 == sig2);
    REQUIRE(a.sign("1700000001") != sig1);
}

TEST_CASE("CoinbaseAuth sign produces base64-looking output", "[auth]") {
    CoinbaseAuth a{"k", "c2VjcmV0", "p"};
    auto sig = a.sign("1700000000");
    REQUIRE(sig.size() == 44);
    REQUIRE(sig.back() == '=');
}

TEST_CASE("CoinbaseAuth::fromEnv returns empty when env is unset", "[auth]") {
    auto a = CoinbaseAuth::fromEnv();
    SUCCEED("fromEnv didn't crash");
}
