// ==============================================================================
// Benchmark Main
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include <benchmark/benchmark.h>
#include <spdlog/spdlog.h>

#include "astra/base/logging.hpp"

int main(int argc, char** argv) {
  astra::base::InitLogging();
  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");

  ::benchmark::Initialize(&argc, argv);
  if (::benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }

  ::benchmark::RunSpecifiedBenchmarks();
  ::benchmark::Shutdown();

  return 0;
}
