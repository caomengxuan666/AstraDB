// ==============================================================================
// BufferPool Unit Tests
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include "astra/core/memory/buffer_pool.hpp"

#include <gtest/gtest.h>

#include <thread>
#include <vector>

namespace astra::core::memory {

// Test: Buffer basic operations
TEST(BufferTest, BasicOperations) {
  Buffer buffer(1024);

  EXPECT_EQ(buffer.capacity(), 1024);
  EXPECT_EQ(buffer.size(), 0);
  EXPECT_TRUE(buffer.empty());

  buffer.Append("Hello", 5);
  EXPECT_EQ(buffer.size(), 5);
  EXPECT_FALSE(buffer.empty());
}

// Test: Buffer resize
TEST(BufferTest, Resize) {
  Buffer buffer(1024);

  buffer.Append("Hello", 5);
  EXPECT_EQ(buffer.size(), 5);

  buffer.Resize(10);
  EXPECT_EQ(buffer.size(), 10);
}

// Test: Buffer reserve
TEST(BufferTest, Reserve) {
  Buffer buffer(100);

  buffer.Append("Hello", 5);
  EXPECT_EQ(buffer.capacity(), 100);

  buffer.Reserve(1000);
  EXPECT_EQ(buffer.capacity(), 1000);
  EXPECT_EQ(buffer.size(), 5);
}

// Test: Buffer reference counting
TEST(BufferTest, ReferenceCounting) {
  Buffer* buffer = new Buffer(1024);

  EXPECT_EQ(buffer->ref_count(), 0);

  {
    BufferPtr ptr1(buffer);
    EXPECT_EQ(buffer->ref_count(), 1);

    {
      BufferPtr ptr2 = ptr1;
      EXPECT_EQ(buffer->ref_count(), 2);
    }

    EXPECT_EQ(buffer->ref_count(), 1);
  }

  // Buffer should be deleted here
}

// Test: BufferPool acquire/release
TEST(BufferPoolTest, AcquireRelease) {
  BufferPool pool;

  BufferPtr buffer1 = pool.Acquire(100);
  EXPECT_NE(buffer1.get(), nullptr);
  EXPECT_EQ(buffer1->capacity(), 100);

  buffer1->Append("Hello", 5);
  EXPECT_EQ(buffer1->size(), 5);

  buffer1.Reset();  // Release to pool
  EXPECT_EQ(pool.GetAvailableBuffers(), 1);
}

// Test: BufferPool statistics
TEST(BufferPoolTest, Statistics) {
  BufferPool pool;

  std::vector<BufferPtr> buffers;

  for (int i = 0; i < 10; ++i) {
    buffers.push_back(pool.Acquire(1000));
  }

  EXPECT_EQ(pool.GetUsedBuffers(), 10);
  EXPECT_EQ(pool.GetAvailableBuffers(), 0);

  buffers.clear();
  EXPECT_EQ(pool.GetUsedBuffers(), 0);
  EXPECT_EQ(pool.GetAvailableBuffers(), 10);
}

// Test: BufferPool thread safety
TEST(BufferPoolTest, ThreadSafety) {
  BufferPool pool;
  const int kNumThreads = 10;
  const int kNumBuffers = 100;

  std::vector<std::thread> threads;
  std::atomic<int> success_count{0};

  for (int t = 0; t < kNumThreads; ++t) {
    threads.emplace_back([&pool, &success_count, kNumBuffers]() {
      std::vector<BufferPtr> buffers;

      for (int i = 0; i < kNumBuffers; ++i) {
        auto buffer = pool.Acquire(1000);
        if (buffer) {
          buffer->Append("test", 4);
          buffers.push_back(std::move(buffer));
          success_count.fetch_add(1);
        }
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_EQ(success_count.load(), kNumThreads * kNumBuffers);
}

// Benchmark: BufferPool vs direct allocation
TEST(BufferPoolTest, DISABLED_BenchmarkBufferPool) {
  const int kIterations = 100000;

  BufferPool pool;

  // BufferPool allocation
  auto start = std::chrono::steady_clock::now();

  std::vector<BufferPtr> buffers;
  for (int i = 0; i < kIterations; ++i) {
    buffers.push_back(pool.Acquire(1000));
  }

  auto end = std::chrono::steady_clock::now();
  auto pool_duration =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start);

  buffers.clear();

  // Direct allocation
  start = std::chrono::steady_clock::now();

  std::vector<Buffer*> direct_buffers;
  for (int i = 0; i < kIterations; ++i) {
    direct_buffers.push_back(new Buffer(1000));
  }

  end = std::chrono::steady_clock::now();
  auto direct_duration =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start);

  for (auto* buf : direct_buffers) {
    delete buf;
  }

  std::cout << "BufferPool: " << pool_duration.count() << " us\n";
  std::cout << "Direct: " << direct_duration.count() << " us\n";

  // BufferPool should be faster (reuses buffers)
}

}  // namespace astra::core::memory
