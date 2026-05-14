#pragma once

#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace eq2::core {

enum class ErrorCode {
  unknown,
  invalid_argument,
  not_found,
  unavailable,
  cancelled,
  timeout,
  parse_error,
};

struct Error {
  ErrorCode code = ErrorCode::unknown;
  std::string message;
};

template <typename T, typename E = Error>
class Result {
 public:
  Result() = delete;

  static auto success(T value) -> Result {
    return Result(std::move(value));
  }

  static auto failure(E error) -> Result {
    return Result(std::move(error));
  }

  [[nodiscard]] auto has_value() const -> bool {
    return std::holds_alternative<T>(value_);
  }

  [[nodiscard]] explicit operator bool() const {
    return has_value();
  }

  auto value() & -> T& {
    if (!has_value()) {
      throw std::logic_error("attempted to read value from failed result");
    }

    return std::get<T>(value_);
  }

  auto value() const& -> const T& {
    if (!has_value()) {
      throw std::logic_error("attempted to read value from failed result");
    }

    return std::get<T>(value_);
  }

  auto error() & -> E& {
    if (has_value()) {
      throw std::logic_error("attempted to read error from successful result");
    }

    return std::get<E>(value_);
  }

  auto error() const& -> const E& {
    if (has_value()) {
      throw std::logic_error("attempted to read error from successful result");
    }

    return std::get<E>(value_);
  }

 private:
  explicit Result(T value) : value_(std::move(value)) {}
  explicit Result(E error) : value_(std::move(error)) {}

  std::variant<T, E> value_;
};

template <typename E>
class Result<void, E> {
 public:
  static auto success() -> Result {
    return Result(true, E{});
  }

  static auto failure(E error) -> Result {
    return Result(false, std::move(error));
  }

  [[nodiscard]] auto has_value() const -> bool {
    return ok_;
  }

  [[nodiscard]] explicit operator bool() const {
    return has_value();
  }

  auto error() & -> E& {
    if (ok_) {
      throw std::logic_error("attempted to read error from successful result");
    }

    return error_;
  }

  auto error() const& -> const E& {
    if (ok_) {
      throw std::logic_error("attempted to read error from successful result");
    }

    return error_;
  }

 private:
  Result(bool ok, E error) : ok_(ok), error_(std::move(error)) {}

  bool ok_ = false;
  E error_;
};

}  // namespace eq2::core
