// ==============================================================================
// SIMD Performance Benchmarks
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include <benchmark/benchmark.h>
#include <string>
#include <vector>
#include <random>
#include "astra/base/simd_utils.hpp"

namespace astra::bench {

// ==============================================================================
// Helper Functions
// ==============================================================================

std::string GenerateTestString(size_t length, bool include_crlf = false) {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(32, 126);  // Printable ASCII
  
  std::string result;
  result.reserve(length);
  
  for (size_t i = 0; i < length; ++i) {
    if (include_crlf && i == length / 2) {
      result += "\r\n";
    } else {
      result.push_back(static_cast<char>(dis(gen)));
    }
  }
  
  return result;
}

std::vector<std::string> GenerateTestStrings(size_t count, size_t avg_length, bool include_crlf = false) {
  std::vector<std::string> strings;
  strings.reserve(count);
  
  for (size_t i = 0; i < count; ++i) {
    strings.push_back(GenerateTestString(avg_length, include_crlf));
  }
  
  return strings;
}

// ==============================================================================
// FindCRLF Benchmarks
// ==============================================================================

static void BM_FindCRLF_Small(benchmark::State& state) {
  auto strings = GenerateTestStrings(1000, 64, true);
  
  for (auto _ : state) {
    for (const auto& str : strings) {
      benchmark::DoNotOptimize(astra::base::simd::FindCRLF(str.data(), str.size()));
    }
  }
  
  state.SetItemsProcessed(state.iterations() * strings.size());
}

static void BM_FindCRLF_Medium(benchmark::State& state) {
  auto strings = GenerateTestStrings(1000, 1024, true);
  
  for (auto _ : state) {
    for (const auto& str : strings) {
      benchmark::DoNotOptimize(astra::base::simd::FindCRLF(str.data(), str.size()));
    }
  }
  
  state.SetItemsProcessed(state.iterations() * strings.size());
}

static void BM_FindCRLF_Large(benchmark::State& state) {
  auto strings = GenerateTestStrings(100, 16384, true);
  
  for (auto _ : state) {
    for (const auto& str : strings) {
      benchmark::DoNotOptimize(astra::base::simd::FindCRLF(str.data(), str.size()));
    }
  }
  
  state.SetItemsProcessed(state.iterations() * strings.size());
}

static void BM_FindCRLF_NoCRLF(benchmark::State& state) {
  auto strings = GenerateTestStrings(1000, 1024, false);
  
  for (auto _ : state) {
    for (const auto& str : strings) {
      benchmark::DoNotOptimize(astra::base::simd::FindCRLF(str.data(), str.size()));
    }
  }
  
  state.SetItemsProcessed(state.iterations() * strings.size());
}

// ==============================================================================
// CaseInsensitiveEquals Benchmarks
// ==============================================================================

static void BM_CaseInsensitiveEquals_Small(benchmark::State& state) {
  auto strings_a = GenerateTestStrings(1000, 64);
  auto strings_b = strings_a;  // Same strings
  
  for (auto _ : state) {
    for (size_t i = 0; i < strings_a.size(); ++i) {
      benchmark::DoNotOptimize(astra::base::simd::CaseInsensitiveEquals(
        strings_a[i].data(), strings_b[i].data(), strings_a[i].size()));
    }
  }
  
  state.SetItemsProcessed(state.iterations() * strings_a.size());
}

static void BM_CaseInsensitiveEquals_Medium(benchmark::State& state) {
  auto strings_a = GenerateTestStrings(1000, 1024);
  auto strings_b = strings_a;  // Same strings
  
  for (auto _ : state) {
    for (size_t i = 0; i < strings_a.size(); ++i) {
      benchmark::DoNotOptimize(astra::base::simd::CaseInsensitiveEquals(
        strings_a[i].data(), strings_b[i].data(), strings_a[i].size()));
    }
  }
  
  state.SetItemsProcessed(state.iterations() * strings_a.size());
}

// ==============================================================================
// HashString Benchmarks
// ==============================================================================

static void BM_HashString_Small(benchmark::State& state) {
  auto strings = GenerateTestStrings(1000, 64);
  
  for (auto _ : state) {
    for (const auto& str : strings) {
      benchmark::DoNotOptimize(astra::base::simd::HashString(str.data(), str.size()));
    }
  }
  
  state.SetItemsProcessed(state.iterations() * strings.size());
}

static void BM_HashString_Medium(benchmark::State& state) {
  auto strings = GenerateTestStrings(1000, 1024);
  
  for (auto _ : state) {
    for (const auto& str : strings) {
      benchmark::DoNotOptimize(astra::base::simd::HashString(str.data(), str.size()));
    }
  }
  
  state.SetItemsProcessed(state.iterations() * strings.size());
}

// ==============================================================================
// MemCompare Benchmarks
// ==============================================================================

static void BM_MemCompare_Small(benchmark::State& state) {
  auto strings_a = GenerateTestStrings(1000, 64);
  auto strings_b = strings_a;  // Same strings
  
  for (auto _ : state) {
    for (size_t i = 0; i < strings_a.size(); ++i) {
      benchmark::DoNotOptimize(astra::base::simd::MemCompare(
        strings_a[i].data(), strings_b[i].data(), strings_a[i].size()));
    }
  }
  
  state.SetItemsProcessed(state.iterations() * strings_a.size());
}

static void BM_MemCompare_Medium(benchmark::State& state) {
  auto strings_a = GenerateTestStrings(1000, 1024);
  auto strings_b = strings_a;  // Same strings
  
  for (auto _ : state) {
    for (size_t i = 0; i < strings_a.size(); ++i) {
      benchmark::DoNotOptimize(astra::base::simd::MemCompare(
        strings_a[i].data(), strings_b[i].data(), strings_a[i].size()));
    }
  }
  
  state.SetItemsProcessed(state.iterations() * strings_a.size());
}

static void BM_MemCompare_Large(benchmark::State& state) {
  auto strings_a = GenerateTestStrings(100, 16384);
  auto strings_b = strings_a;  // Same strings
  
  for (auto _ : state) {
    for (size_t i = 0; i < strings_a.size(); ++i) {
      benchmark::DoNotOptimize(astra::base::simd::MemCompare(
        strings_a[i].data(), strings_b[i].data(), strings_a[i].size()));
    }
  }
  
  state.SetItemsProcessed(state.iterations() * strings_a.size());
}

// ==============================================================================
// HasZero Benchmarks
// ==============================================================================

static void BM_HasZero_Small(benchmark::State& state) {
  auto strings = GenerateTestStrings(1000, 64);
  
  for (auto _ : state) {
    for (const auto& str : strings) {
      benchmark::DoNotOptimize(astra::base::simd::HasZero(str.data(), str.size()));
    }
  }
  
  state.SetItemsProcessed(state.iterations() * strings.size());
}

static void BM_HasZero_Medium(benchmark::State& state) {
  auto strings = GenerateTestStrings(1000, 1024);
  
  for (auto _ : state) {
    for (const auto& str : strings) {
      benchmark::DoNotOptimize(astra::base::simd::HasZero(str.data(), str.size()));
    }
  }
  
  state.SetItemsProcessed(state.iterations() * strings.size());
}

// ==============================================================================
// Register Benchmarks
// ==============================================================================

BENCHMARK(BM_FindCRLF_Small);
BENCHMARK(BM_FindCRLF_Medium);
BENCHMARK(BM_FindCRLF_Large);
BENCHMARK(BM_FindCRLF_NoCRLF);

BENCHMARK(BM_CaseInsensitiveEquals_Small);
BENCHMARK(BM_CaseInsensitiveEquals_Medium);

BENCHMARK(BM_HashString_Small);
BENCHMARK(BM_HashString_Medium);

BENCHMARK(BM_MemCompare_Small);
BENCHMARK(BM_MemCompare_Medium);
BENCHMARK(BM_MemCompare_Large);

BENCHMARK(BM_HasZero_Small);
BENCHMARK(BM_HasZero_Medium);

}  // namespace astra::bench