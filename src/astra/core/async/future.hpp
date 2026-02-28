// ==============================================================================
// Future Utilities
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <asio/awaitable.hpp>
#include <asio/use_awaitable.hpp>
#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/str_cat.h>
#include <optional>
#include <variant>
#include <system_error>

#include "astra/base/macros.hpp"

namespace astra::core::async {

// ==============================================================================
// AsyncResult - Result type for async operations
// ==============================================================================
// Similar to absl::StatusOr but for async operations
// Can hold a value, an error, or be pending
// ==============================================================================

template <typename T>
class AsyncResult {
 public:
  using ValueType = T;

  // Constructors
  AsyncResult() = default;

  explicit AsyncResult(T value) 
      : state_(std::move(value)) {}

  explicit AsyncResult(absl::Status status)
      : state_(std::move(status)) {
    DCHECK(!status.ok());
  }

  // Status check
  bool ok() const {
    return std::holds_alternative<T>(state_);
  }

  // Get the status (error if not ok)
  absl::Status status() const {
    if (ok()) {
      return absl::OkStatus();
    }
    return std::get<absl::Status>(state_);
  }

  // Get the value (must check ok() first)
  const T& value() const& {
    DCHECK(ok());
    return std::get<T>(state_);
  }

  T& value() & {
    DCHECK(ok());
    return std::get<T>(state_);
  }

  T&& value() && {
    DCHECK(ok());
    return std::get<T>(std::move(state_));
  }

  // Get value or default
  T value_or(T default_value) const {
    if (ok()) {
      return std::get<T>(state_);
    }
    return default_value;
  }

 private:
  std::variant<T, absl::Status> state_;
};

// Specialization for void
template <>
class AsyncResult<void> {
 public:
  AsyncResult() : status_(absl::OkStatus()) {}

  explicit AsyncResult(absl::Status status)
      : status_(std::move(status)) {
    DCHECK(!status.ok());
  }

  bool ok() const { return status_.ok(); }
  absl::Status status() const { return status_; }

 private:
  absl::Status status_;
};

// ==============================================================================
// AsyncError - Async error handling utilities
// ==============================================================================

class AsyncError {
 public:
  static absl::Status FromSystemError(const std::system_error& e) {
    return absl::InternalError(
        absl::StrCat("System error: ", e.what(), " (", e.code().message(), ")"));
  }

  static absl::Status Cancelled() {
    return absl::CancelledError("Operation cancelled");
  }

  static absl::Status DeadlineExceeded() {
    return absl::DeadlineExceededError("Operation timed out");
  }

  static absl::Status Unavailable() {
    return absl::UnavailableError("Service unavailable");
  }
};

// ==============================================================================
// Async Helpers
// ==============================================================================

// Convert system error to Status
inline absl::Status ToStatus(const std::error_code& ec) {
  if (!ec) {
    return absl::OkStatus();
  }
  return absl::InternalError(absl::StrCat("Error: ", ec.message()));
}

// Sleep for a duration (awaitable)
template <typename Rep, typename Period>
inline asio::awaitable<void> AsyncSleep(
    std::chrono::duration<Rep, Period> duration) {
  asio::steady_timer timer(co_await asio::this_coro::executor);
  timer.expires_after(duration);
  co_await timer.async_wait(asio::use_awaitable);
}

// Yield control to the executor
inline asio::awaitable<void> AsyncYield() {
  asio::steady_timer timer(co_await asio::this_coro::executor);
  timer.expires_after(std::chrono::milliseconds(0));
  co_await timer.async_wait(asio::use_awaitable);
}

// Run a function with timeout
template <typename Func>
inline asio::awaitable<AsyncResult<std::invoke_result_t<Func>>> AsyncWithTimeout(
    Func&& func,
    std::chrono::milliseconds timeout) {
  asio::steady_timer timer(co_await asio::this_coro::executor);
  timer.expires_after(timeout);
  
  try {
    auto result = co_await func();
    co_return AsyncResult<decltype(result)>(std::move(result));
  } catch (const std::exception& e) {
    co_return AsyncResult<decltype(result)>(
        absl::InternalError(absl::StrCat("Exception: ", e.what())));
  }
}

} // namespace astra::core::async