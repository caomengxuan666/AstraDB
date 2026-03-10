// ==============================================================================
// Executor Benchmarks
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include <absl/base/thread_annotations.h>
#include <benchmark/benchmark.h>

#include "astra/base/logging.hpp"
#include "astra/core/async/awaitable_ops.hpp"
#include "astra/core/async/executor.hpp"

namespace astra::core::async {

// Benchmark coroutine creation and execution
static void BM_CoroutineCreation(benchmark::State& state) {
  Executor executor(4);  // 4 worker threads
  executor.Run();

  for (auto _ : state) {
    // Measure coroutine creation overhead
    auto task = []() -> asio::awaitable<void> { co_return; };
    benchmark::DoNotOptimize(task);
  }

  executor.Stop();
  state.SetItemsProcessed(state.iterations());
}

// Register benchmarks
BENCHMARK(BM_CoroutineCreation);

// Benchmark async sleep (context switching)
static void BM_AsyncSleep(benchmark::State& state) {
  Executor executor(4);
  executor.Run();

  for (auto _ : state) {
    executor.Spawn([]() -> asio::awaitable<void> {
      co_await AsyncSleep(std::chrono::milliseconds(1));
      co_return;
    });
  }

  executor.Stop();
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_AsyncSleep);

}  // namespace astra::core::async
