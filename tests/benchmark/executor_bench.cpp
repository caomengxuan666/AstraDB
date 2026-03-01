// ==============================================================================
// Executor Benchmarks
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include <benchmark/benchmark.h>
#include "astra/core/async/executor.hpp"
#include "astra/core/async/awaitable_ops.hpp"
#include <absl/base/thread_annotations.h>

namespace astra::core::async {

// Benchmark coroutine creation and execution
static void BM_CoroutineCreation(benchmark::State& state) {
  Executor executor(4);  // 4 worker threads
  
  for (auto _ : state) {
    // Measure coroutine creation overhead
    auto task = []() -> asio::awaitable<void> {
      co_return;
    };
    benchmark::DoNotOptimize(task);
  }
  
  state.SetItemsProcessed(state.iterations());
}

// Register benchmarks
BENCHMARK(BM_CoroutineCreation);

// Benchmark async sleep (context switching)
static void BM_AsyncSleep(benchmark::State& state) {
  Executor executor(4);
  
  for (auto _ : state) {
    executor.Submit([]() -> asio::awaitable<void> {
      co_await AsyncSleep(std::chrono::microseconds(1));
    });
  }
  
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_AsyncSleep);

}  // namespace astra::core::async

BENCHMARK_MAIN();