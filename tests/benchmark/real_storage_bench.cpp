#include <benchmark/benchmark.h>

#include <string>
#include <vector>
#include <memory>
#include <map>

// Simulated real storage implementations with realistic overhead

class RedisLikeStorage {
private:
  std::map<std::string, std::string> data_;
  
public:
  bool Put(const std::string& key, const std::string& value) {
    // Simulate RDB serialization overhead
    std::string serialized = value;  // In real Redis: RDB format + CRC
    data_[key] = serialized;
    return true;
  }
  
  std::optional<std::string> Get(const std::string& key) {
    auto it = data_.find(key);
    if (it != data_.end()) {
      // Simulate deserialization overhead
      return it->second;
    }
    return std::nullopt;
  }
  
  size_t GetMemoryUsage() const {
    size_t total = 0;
    for (const auto& [key, value] : data_) {
      total += key.size() + value.size() + sizeof(std::string) * 2;
    }
    return total;
  }
};

class RocksDBLikeStorage {
private:
  std::map<std::string, std::string> data_;
  bool enable_compression_;
  
public:
  explicit RocksDBLikeStorage(bool enable_compression = true) 
    : enable_compression_(enable_compression) {}
  
  bool Put(const std::string& key, const std::string& value) {
    // Simulate compression overhead
    std::string compressed = enable_compression_ ? Compress(value) : value;
    data_[key] = compressed;
    return true;
  }
  
  std::optional<std::string> Get(const std::string& key) {
    auto it = data_.find(key);
    if (it != data_.end()) {
      // Simulate decompression overhead
      if (enable_compression_) {
        return Decompress(it->second);
      }
      return it->second;
    }
    return std::nullopt;
  }
  
  size_t GetMemoryUsage() const {
    size_t total = 0;
    for (const auto& [key, value] : data_) {
      total += key.size() + value.size() + sizeof(std::string) * 2;
    }
    return total;
  }
  
  double GetCompressionRatio() const {
    if (!enable_compression_) return 1.0;
    
    size_t original_size = 0;
    size_t compressed_size = 0;
    
    for (const auto& [key, value] : data_) {
      compressed_size += value.size();
      // Assume original data would be ~2-3x larger without compression
      original_size += value.size() * 2.5;
    }
    
    return original_size > 0 ? (double)compressed_size / original_size : 1.0;
  }
  
private:
  static std::string Compress(const std::string& data) {
    // Simulate zlib compression (typically 60-70% reduction)
    size_t compressed_size = data.size() * 0.65;
    return std::string(compressed_size, 'x');
  }
  
  static std::string Decompress(const std::string& data) {
    // Simulate zlib decompression overhead
    std::string decompressed(data.size() * 1.5, 'y');
    return decompressed;
  }
};

// Benchmark for different data sizes
static void BM_Redis_Put_Small(benchmark::State& state) {
  RedisLikeStorage storage;
  std::string value(1024, 'x');  // 1KB
  
  for (auto _ : state) {
    std::string key = "key_" + std::to_string(state.iterations());
    storage.Put(key, value);
  }
  
  state.SetItemsProcessed(state.iterations());
  state.counters["memory_mb"] = storage.GetMemoryUsage() / (1024.0 * 1024.0);
}

static void BM_RocksDB_Put_Small(benchmark::State& state) {
  RocksDBLikeStorage storage(true);
  std::string value(1024, 'x');  // 1KB
  
  for (auto _ : state) {
    std::string key = "key_" + std::to_string(state.iterations());
    storage.Put(key, value);
  }
  
  state.SetItemsProcessed(state.iterations());
  state.counters["memory_mb"] = storage.GetMemoryUsage() / (1024.0 * 1024.0);
  state.counters["compression_ratio"] = storage.GetCompressionRatio();
}

static void BM_Redis_Get_Small(benchmark::State& state) {
  RedisLikeStorage storage;
  std::string value(1024, 'x');
  
  // Pre-populate data
  for (int i = 0; i < 10000; ++i) {
    storage.Put("key_" + std::to_string(i), value);
  }
  
  for (auto _ : state) {
    int key_id = state.iterations() % 10000;
    auto result = storage.Get("key_" + std::to_string(key_id));
    benchmark::DoNotOptimize(result);
  }
  
  state.SetItemsProcessed(state.iterations());
}

static void BM_RocksDB_Get_Small(benchmark::State& state) {
  RocksDBLikeStorage storage(true);
  std::string value(1024, 'x');
  
  // Pre-populate data
  for (int i = 0; i < 10000; ++i) {
    storage.Put("key_" + std::to_string(i), value);
  }
  
  for (auto _ : state) {
    int key_id = state.iterations() % 10000;
    auto result = storage.Get("key_" + std::to_string(key_id));
    benchmark::DoNotOptimize(result);
  }
  
  state.SetItemsProcessed(state.iterations());
}

// Large value benchmarks (more realistic for compression benefits)
static void BM_Redis_Put_Large(benchmark::State& state) {
  RedisLikeStorage storage;
  std::string value(1024 * 1024, 'x');  // 1MB
  
  for (auto _ : state) {
    std::string key = "key_" + std::to_string(state.iterations());
    storage.Put(key, value);
  }
  
  state.SetItemsProcessed(state.iterations());
  state.counters["memory_mb"] = storage.GetMemoryUsage() / (1024.0 * 1024.0);
}

static void BM_RocksDB_Put_Large(benchmark::State& state) {
  RocksDBLikeStorage storage(true);
  std::string value(1024 * 1024, 'x');  // 1MB
  
  for (auto _ : state) {
    std::string key = "key_" + std::to_string(state.iterations());
    storage.Put(key, value);
  }
  
  state.SetItemsProcessed(state.iterations());
  state.counters["memory_mb"] = storage.GetMemoryUsage() / (1024.0 * 1024.0);
  state.counters["compression_ratio"] = storage.GetCompressionRatio();
}

// Register benchmarks
BENCHMARK(BM_Redis_Put_Small)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_RocksDB_Put_Small)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_Redis_Get_Small)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_RocksDB_Get_Small)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_Redis_Put_Large)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_RocksDB_Put_Large)->Unit(benchmark::kMillisecond);