#pragma once

#include "trading/core/result.hpp"
#include "trading/md/coinbase_messages.hpp"

#include <simdjson.h>

#include <string>
#include <string_view>

namespace trading {

class CoinbaseParser {
public:
    CoinbaseParser();

    Error parse(std::string_view json, CoinbaseMessage& out) noexcept;

private:
    simdjson::ondemand::parser parser_;
    std::string padded_;
};

} // namespace trading
