#pragma once

#include <string_view>

namespace trading {

enum class Error : int {
    Ok = 0,

    ConfigFileNotFound,
    ConfigInvalidValue,

    ParseUnexpectedShape,
    ParseMalformedJson,
};

constexpr std::string_view toString(Error e) noexcept {
    switch (e) {
        case Error::Ok: return "Ok";
        case Error::ConfigFileNotFound: return "ConfigFileNotFound";
        case Error::ConfigInvalidValue: return "ConfigInvalidValue";
        case Error::ParseUnexpectedShape: return "ParseUnexpectedShape";
        case Error::ParseMalformedJson: return "ParseMalformedJson";
    }
    return "Unknown";
}

}  // namespace trading
