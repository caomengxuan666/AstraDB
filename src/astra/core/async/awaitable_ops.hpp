// ==============================================================================
// Awaitable Operations
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/strand.hpp>
#include <asio/use_awaitable.hpp>
#include <absl/functional/function_ref.h>
#include <coroutine>
#include <optional>
#include <variant>

#include "astra/base/macros.hpp"

namespace astra::core::async {

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

// ==============================================================================
// Awaitable utilities
// ==============================================================================

// Try an awaitable and catch exceptions
template <typename Awaitable>
inline asio::awaitable<std::optional<typename std::decay_t<
    decltype(co_await std::declval<Awaitable>())>>> TryAwaitable(Awaitable&& awaitable) {
  try {
    co_return co_await std::forward<Awaitable>(awaitable);
  } catch (...) {
    co_return std::nullopt;
  }
}

// Retry an awaitable with exponential backoff
template <typename Awaitable>
inline asio::awaitable<std::optional<typename std::decay_t<
    decltype(co_await std::declval<Awaitable>())>>> RetryWithBackoff(
    Awaitable&& awaitable,
    int max_retries,
    std::chrono::milliseconds initial_delay) {
  auto delay = initial_delay;
  
  for (int i = 0; i <= max_retries; ++i) {
    try {
      co_return co_await std::forward<Awaitable>(awaitable);
    } catch (const std::exception& e) {
      if (i == max_retries) {
        co_return std::nullopt;
      }
      
      // Wait with exponential backoff
      asio::steady_timer timer(co_await asio::this_coro::executor);
      timer.expires_after(delay);
      co_await timer.async_wait(asio::use_awaitable);
      
      delay *= 2;
    }
  }
  
  co_return std::nullopt;
}

// Race multiple awaitables and return the first to complete
template <typename... Awaitables>
inline asio::awaitable<std::variant<
    typename std::decay_t<decltype(co_await std::declval<Awaitables>())>...>>
Race(Awaitables&&... awaitables) {
  // Implementation would use asio::experimental::make_parallel_group
  // For now, this is a placeholder
  static_assert(sizeof...(Awaitables) == 1, "Race not fully implemented yet");
  co_return std::variant<
      typename std::decay_t<decltype(co_await std::declval<Awaitables>())>...>(
          co_await std::get<0>(std::forward_as_tuple(awaitables...)));
}

// Execute multiple awaitables in parallel and collect results
template <typename... Awaitables>
inline asio::awaitable<std::tuple<
    typename std::decay_t<decltype(co_await std::declval<Awaitables>())>...>>
All(Awaitables&&... awaitables) {
  // Implementation would use asio::experimental::make_parallel_group
  // For now, execute sequentially
  co_return std::tuple<
      typename std::decay_t<decltype(co_await std::declval<Awaitables>())>...>(
          co_await std::get<0>(std::forward_as_tuple(awaitables...)));
}

// Transform an awaitable's result
template <typename Awaitable, typename Transform>
inline asio::awaitable<std::invoke_result_t<Transform,
    typename std::decay_t<decltype(co_await std::declval<Awaitable>())>>>
TransformAwaitable(Awaitable&& awaitable, Transform&& transform) {
  auto result = co_await std::forward<Awaitable>(awaitable);
  co_return std::invoke(std::forward<Transform>(transform), std::move(result));
}

// Filter an awaitable's result
template <typename Awaitable, typename Predicate>
inline asio::awaitable<std::optional<typename std::decay_t<
    decltype(co_await std::declval<Awaitable>())>>> FilterAwaitable(
    Awaitable&& awaitable,
    Predicate&& predicate) {
  auto result = co_await std::forward<Awaitable>(awaitable);
  if (std::invoke(std::forward<Predicate>(predicate), result)) {
    co_return std::move(result);
  }
  co_return std::nullopt;
}

// Chain multiple awaitables
template <typename Awaitable, typename... Funcs>
inline auto ChainAwaitable(Awaitable&& awaitable, Funcs&&... funcs) {
  // Placeholder implementation
  return std::forward<Awaitable>(awaitable);
}

} // namespace astra::core::async