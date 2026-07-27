#include "trading/md/coinbase_auth.hpp"

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <array>
#include <cstdlib>
#include <string>

namespace trading {

namespace {

std::string base64Decode(std::string_view in) noexcept {
    std::string out;
    out.resize(((in.size() + 3) / 4) * 3);
    int n = EVP_DecodeBlock(reinterpret_cast<unsigned char*>(out.data()),
                            reinterpret_cast<const unsigned char*>(in.data()),
                            static_cast<int>(in.size()));
    if (n < 0) return "";
    size_t pad = 0;
    for (auto it = in.rbegin(); it != in.rend() && *it == '='; ++it)
        ++pad;
    out.resize(static_cast<size_t>(n) - pad);
    return out;
}

std::string base64Encode(const unsigned char* data, size_t n) noexcept {
    std::string out;
    out.resize((n + 2) / 3 * 4);
    int written =
        EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()), data, static_cast<int>(n));
    if (written < 0) return "";
    out.resize(static_cast<size_t>(written));
    return out;
}

}  // namespace

std::string CoinbaseAuth::sign(std::string_view timestamp) const noexcept {
    if (!hasCredentials()) return {};

    std::string secret = base64Decode(secret_b64_);
    if (secret.empty()) return {};

    std::string prehash;
    prehash.reserve(timestamp.size() + 32);
    prehash.append(timestamp);
    prehash.append("GET/users/self/verify");

    std::array<unsigned char, EVP_MAX_MD_SIZE> mac{};
    unsigned int maclen = 0;
    HMAC(EVP_sha256(), secret.data(), static_cast<int>(secret.size()),
         reinterpret_cast<const unsigned char*>(prehash.data()), prehash.size(), mac.data(),
         &maclen);

    return base64Encode(mac.data(), maclen);
}

CoinbaseAuth CoinbaseAuth::fromEnv() noexcept {
    const char* key = std::getenv("COINBASE_API_KEY");
    const char* secret = std::getenv("COINBASE_API_SECRET");
    const char* pass = std::getenv("COINBASE_API_PASSPHRASE");
    if (!key || !secret) return CoinbaseAuth{};
    return CoinbaseAuth{std::string{key}, std::string{secret}, std::string{pass ? pass : ""}};
}

}  // namespace trading
