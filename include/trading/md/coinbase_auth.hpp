#pragma once

#include <string>
#include <string_view>

namespace trading {

class CoinbaseAuth {
public:
    CoinbaseAuth() = default;
    CoinbaseAuth(std::string key, std::string secretB64, std::string passphrase) noexcept
        : key_(std::move(key)),
          secret_b64_(std::move(secretB64)),
          passphrase_(std::move(passphrase)) {}

    bool hasCredentials() const noexcept { return !key_.empty() && !secret_b64_.empty(); }

    const std::string& key() const noexcept { return key_; }
    const std::string& passphrase() const noexcept { return passphrase_; }

    std::string sign(std::string_view timestamp) const noexcept;

    static CoinbaseAuth fromEnv() noexcept;

private:
    std::string key_;
    std::string secret_b64_;
    std::string passphrase_;
};

} // namespace trading
