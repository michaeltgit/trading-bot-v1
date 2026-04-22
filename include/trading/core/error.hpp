#pragma once

#include <string_view>

namespace trading {

enum class Error : int {
    Ok = 0,

    ConfigFileNotFound,
    ConfigInvalid,
    ConfigMissingKey,
    ConfigInvalidValue,

    NetworkConnectFailed,
    NetworkReadFailed,
    NetworkWriteFailed,
    NetworkTlsFailed,
    NetworkClosed,

    ParseFailed,
    ParseUnexpectedShape,
    ParseMalformedJson,

    SequenceGap,
    HeartbeatLost,
    SubscribeRejected,

    OrderRejectedByRisk,
    OrderBookEmpty,
    PoolExhausted,
    QueueFull,

    SymbolUnknown,
};

constexpr std::string_view toString(Error e) noexcept {
    switch (e) {
        case Error::Ok: return "Ok";
        case Error::ConfigFileNotFound: return "ConfigFileNotFound";
        case Error::ConfigInvalid: return "ConfigInvalid";
        case Error::ConfigMissingKey: return "ConfigMissingKey";
        case Error::ConfigInvalidValue: return "ConfigInvalidValue";
        case Error::NetworkConnectFailed: return "NetworkConnectFailed";
        case Error::NetworkReadFailed: return "NetworkReadFailed";
        case Error::NetworkWriteFailed: return "NetworkWriteFailed";
        case Error::NetworkTlsFailed: return "NetworkTlsFailed";
        case Error::NetworkClosed: return "NetworkClosed";
        case Error::ParseFailed: return "ParseFailed";
        case Error::ParseUnexpectedShape: return "ParseUnexpectedShape";
        case Error::ParseMalformedJson: return "ParseMalformedJson";
        case Error::SequenceGap: return "SequenceGap";
        case Error::HeartbeatLost: return "HeartbeatLost";
        case Error::SubscribeRejected: return "SubscribeRejected";
        case Error::OrderRejectedByRisk: return "OrderRejectedByRisk";
        case Error::OrderBookEmpty: return "OrderBookEmpty";
        case Error::PoolExhausted: return "PoolExhausted";
        case Error::QueueFull: return "QueueFull";
        case Error::SymbolUnknown: return "SymbolUnknown";
    }
    return "Unknown";
}

} // namespace trading
