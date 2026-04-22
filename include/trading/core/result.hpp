#pragma once

#include "trading/core/error.hpp"

#include <type_traits>
#include <utility>
#include <variant>

namespace trading {

template <typename T>
class Result {
public:
    Result(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : state_(std::move(value)) {}
    Result(Error err) noexcept : state_(err) {}

    bool ok() const noexcept { return std::holds_alternative<T>(state_); }
    explicit operator bool() const noexcept { return ok(); }

    T& value() & noexcept { return std::get<T>(state_); }
    const T& value() const& noexcept { return std::get<T>(state_); }
    T&& value() && noexcept { return std::get<T>(std::move(state_)); }

    Error error() const noexcept {
        return ok() ? Error::Ok : std::get<Error>(state_);
    }

private:
    std::variant<T, Error> state_;
};

template <>
class Result<void> {
public:
    Result() noexcept : err_(Error::Ok) {}
    Result(Error err) noexcept : err_(err) {}

    bool ok() const noexcept { return err_ == Error::Ok; }
    explicit operator bool() const noexcept { return ok(); }
    Error error() const noexcept { return err_; }

private:
    Error err_;
};

} // namespace trading
