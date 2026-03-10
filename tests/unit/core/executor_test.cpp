// ==============================================================================
// Executor Unit Tests
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include "astra/core/async/executor.hpp"

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/use_awaitable.hpp>
#include <chrono>
#include <thread>

namespace astra::core::async {

// Test: Executor construction and destruction
TEST(ExecutorTest, Construction) {
  Executor executor(2);
  EXPECT_EQ(executor.GetThreadCount(), 0);  // Not started yet
}

// Test: Executor start and stop
TEST(ExecutorTest, StartStop) {
  Executor executor(2);
  executor.Run();
  EXPECT_EQ(executor.GetThreadCount(), 2);
  executor.Stop();
  EXPECT_EQ(executor.GetThreadCount(), 0);
}

// Test: Post tasks
TEST(ExecutorTest, PostTasks) {
  Executor executor(2);
  executor.Run();

  std::atomic<int> counter{0};

  for (int i = 0; i < 100; ++i) {
    executor.Post(
        [&counter]() { counter.fetch_add(1, std::memory_order_relaxed); });
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_EQ(counter.load(), 100);

  executor.Stop();
}

// Test: Spawn coroutines
TEST(ExecutorTest, SpawnCoroutines) {
  Executor executor(2);
  executor.Run();

  std::atomic<int> counter{0};

  auto coroutine = [&counter]() -> asio::awaitable<void> {
    counter.fetch_add(1, std::memory_order_relaxed);
    co_return;
  };

  for (int i = 0; i < 100; ++i) {
    executor.Spawn(coroutine(), "test_coroutine");
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_EQ(counter.load(), 100);

  executor.Stop();
}

// Test: Async sleep
TEST(ExecutorTest, AsyncSleep) {
  Executor executor(2);
  executor.Run();

  auto test_sleep = []() -> asio::awaitable<void> {
    auto start = std::chrono::steady_clock::now();
    co_await AsyncSleep(std::chrono::milliseconds(50));
    auto end = std::chrono::steady_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    EXPECT_GE(duration.count(), 45);  // Allow some tolerance
  };

  executor.Spawn(test_sleep(), "sleep_test");

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  executor.Stop();
}

// Test: Thread index
TEST(ExecutorTest, ThreadIndex) {
  Executor executor(4);
  executor.Run();

  std::atomic<bool> all_threads_used{false};
  std::array<std::atomic<bool>, 4> thread_used{};

  for (auto& used : thread_used) {
    used.store(false);
  }

  auto check_threads = [&thread_used,
                        &all_threads_used]() -> asio::awaitable<void> {
    for (int i = 0; i < 100; ++i) {
      size_t idx = std::hash<std::thread::id>{}(std::this_thread::get_id()) % 4;
      thread_used[idx].store(true);
      co_await AsyncYield();
    }
  };

  for (int i = 0; i < 10; ++i) {
    executor.Spawn(check_threads(), "thread_check");
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  bool all_used = true;
  for (const auto& used : thread_used) {
    all_used = all_used && used.load();
  }

  EXPECT_TRUE(all_used);
  executor.Stop();
}

}  // namespace astra::core::async
