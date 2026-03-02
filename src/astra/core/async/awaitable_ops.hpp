// ==============================================================================
// Awaitable Operations
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/steady_timer.hpp>
#include <asio/strand.hpp>
#include <asio/use_awaitable.hpp>
#include <absl/functional/function_ref.h>
#include <chrono>
#include <coroutine>
#include <optional>

#include "astra/base/macros.hpp"

namespace astra::core::async {

// ==============================================================================
// Async Sleep
// ==============================================================================
inline asio::awaitable<void> AsyncSleep(std::chrono::milliseconds duration) {
  asio::steady_timer timer(co_await asio::this_coro::executor);
  timer.expires_after(duration);
  co_await timer.async_wait(asio::use_awaitable);
}

// ==============================================================================
// AwaitableGuard - RAII guard for awaitable operations
// ==============================================================================
template <typename T>
class AwaitableGuard {
 public:
  explicit AwaitableGuard(T&& value) : value_(std::forward<T>(value)) {}
  ~AwaitableGuard() = default;

  T& get() { return value_; }
  const T& get() const { return value_; }

  AwaitableGuard(const AwaitableGuard&) = delete;
  AwaitableGuard& operator=(const AwaitableGuard&) = delete;
  AwaitableGuard(AwaitableGuard&&) = default;
  AwaitableGuard& operator=(AwaitableGuard&&) = default;

 private:
  T value_;
};

} // namespace astra::core::async