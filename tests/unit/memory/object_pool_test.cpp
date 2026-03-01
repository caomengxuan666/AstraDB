// ==============================================================================
// ObjectPool Unit Tests
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include <gtest/gtest.h>
#include "astra/core/memory/object_pool.hpp"
#include <thread>
#include <vector>

namespace astra::core::memory {

// Test: ObjectPool basic usage
TEST(ObjectPoolTest, BasicUsage) {
  ObjectPool<int> pool(10);
  
  auto obj1 = pool.Acquire();
  EXPECT_NE(obj1.get(), nullptr);
  EXPECT_EQ(pool.GetUsed(), 1);
  
  *obj1 = 42;
  EXPECT_EQ(*obj1, 42);
}

// Test: ObjectPool reuse
TEST(ObjectPoolTest, Reuse) {
  ObjectPool<int> pool(10);
  
  int* raw_ptr = nullptr;
  
  {
    auto obj1 = pool.Acquire();
    raw_ptr = obj1.get();
    *obj1 = 100;
  }
  
  // Object should be returned to pool
  EXPECT_EQ(pool.GetAvailable(), 1);
  
  {
    auto obj2 = pool.Acquire();
    // Should reuse the same object
    EXPECT_EQ(obj2.get(), raw_ptr);
    EXPECT_EQ(*obj2, 100);  // Value should be preserved (unless reset)
  }
}

// Test: ObjectPool thread safety
TEST(ObjectPoolTest, ThreadSafety) {
  ObjectPool<int> pool(100);
  const int kNumThreads = 10;
  const int kNumOperations = 1000;
  
  std::vector<std::thread> threads;
  std::atomic<int> success_count{0};
  
  for (int t = 0; t < kNumThreads; ++t) {
    threads.emplace_back([&pool, &success_count, kNumOperations]() {
      for (int i = 0; i < kNumOperations; ++i) {
        auto obj = pool.Acquire();
        if (obj) {
          *obj = i;
          success_count.fetch_add(1);
        }
      }
    });
  }
  
  for (auto& thread : threads) {
    thread.join();
  }
  
  EXPECT_EQ(success_count.load(), kNumThreads * kNumOperations);
}

// Test: SimpleObjectPool
TEST(SimpleObjectPoolTest, BasicUsage) {
  SimpleObjectPool<int> pool(10);
  
  int* obj1 = pool.Acquire();
  EXPECT_NE(obj1, nullptr);
  EXPECT_EQ(pool.GetAvailable(), 9);
  
  pool.Release(obj1);
  EXPECT_EQ(pool.GetAvailable(), 10);
}

// Benchmark: ObjectPool vs new/delete
TEST(ObjectPoolTest, DISABLED_BenchmarkObjectPool) {
  const int kIterations = 100000;
  
  ObjectPool<int> pool(1000);
  
  // ObjectPool allocation
  auto start = std::chrono::steady_clock::now();
  
  std::vector<decltype(pool.Acquire())> objects;
  for (int i = 0; i < kIterations; ++i) {
    objects.push_back(pool.Acquire());
  }
  
  auto end = std::chrono::steady_clock::now();
  auto pool_duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  
  objects.clear();
  
  // Direct new/delete
  start = std::chrono::steady_clock::now();
  
  std::vector<int*> direct_objects;
  for (int i = 0; i < kIterations; ++i) {
    direct_objects.push_back(new int());
  }
  
  for (auto* obj : direct_objects) {
    delete obj;
  }
  
  end = std::chrono::steady_clock::now();
  auto direct_duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  
  std::cout << "ObjectPool: " << pool_duration.count() << " us\n";
  std::cout << "Direct: " << direct_duration.count() << " us\n";
  
  // ObjectPool should be faster
}

// Test: ObjectPool with custom factory
TEST(ObjectPoolTest, CustomFactory) {
  ObjectPool<std::string> pool(
      10,
      []() { return new std::string("default"); },
      [](std::string* s) { delete s; });
  
  auto obj = pool.Acquire();
  EXPECT_EQ(*obj, "default");
  
  *obj = "custom";
  EXPECT_EQ(*obj, "custom");
}

} // namespace astra::core::memory