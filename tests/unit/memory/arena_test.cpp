// ==============================================================================
// Arena Unit Tests
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include <gtest/gtest.h>
#include "astra/core/memory/arena_allocator.hpp"
#include <vector>
#include <string>

namespace astra::core::memory {

// Test: Arena basic allocation
TEST(ArenaTest, BasicAllocation) {
  Arena arena(1024);
  
  void* ptr1 = arena.Allocate(100);
  void* ptr2 = arena.Allocate(200);
  
  EXPECT_NE(ptr1, nullptr);
  EXPECT_NE(ptr2, nullptr);
  EXPECT_NE(ptr1, ptr2);
  
  EXPECT_EQ(arena.GetAllocatedBytes(), 300);
}

// Test: Arena aligned allocation
TEST(ArenaTest, AlignedAllocation) {
  Arena arena(1024);
  
  void* ptr1 = arena.AllocateAligned(100, 16);
  void* ptr2 = arena.AllocateAligned(100, 32);
  
  EXPECT_NE(ptr1, nullptr);
  EXPECT_NE(ptr2, nullptr);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr1) % 16, 0);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr2) % 32, 0);
}

// Test: Arena reset
TEST(ArenaTest, Reset) {
  Arena arena(1024);
  
  arena.Allocate(100);
  arena.Allocate(200);
  EXPECT_EQ(arena.GetAllocatedBytes(), 300);
  
  arena.Reset();
  EXPECT_EQ(arena.GetAllocatedBytes(), 0);
}

// Test: Arena multiple blocks
TEST(ArenaTest, MultipleBlocks) {
  Arena arena(512);
  
  // Allocate more than one block
  for (int i = 0; i < 100; ++i) {
    arena.Allocate(100);
  }
  
  EXPECT_GT(arena.GetBlockCount(), 1);
}

// Test: ArenaAllocator with vector
TEST(ArenaAllocatorTest, WithVector) {
  Arena arena(4096);
  ArenaAllocator<int> allocator(&arena);
  
  std::vector<int, ArenaAllocator<int>> vec(allocator);
  
  for (int i = 0; i < 100; ++i) {
    vec.push_back(i);
  }
  
  EXPECT_EQ(vec.size(), 100);
  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(vec[i], i);
  }
}

// Test: ArenaAllocator with string
TEST(ArenaAllocatorTest, WithString) {
  Arena arena(4096);
  ArenaAllocator<char> allocator(&arena);
  
  std::basic_string<char, std::char_traits<char>, ArenaAllocator<char>> 
      str(allocator);
  
  str = "Hello, AstraDB!";
  EXPECT_EQ(str, "Hello, AstraDB!");
}

// Benchmark: Arena vs malloc
TEST(ArenaTest, DISABLED_BenchmarkArenaVsMalloc) {
  const int kIterations = 100000;
  
  // Arena allocation
  Arena arena(1024 * 1024);
  auto start = std::chrono::steady_clock::now();
  
  for (int i = 0; i < kIterations; ++i) {
    arena.Allocate(100);
  }
  
  auto end = std::chrono::steady_clock::now();
  auto arena_duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  
  // Malloc allocation
  start = std::chrono::steady_clock::now();
  std::vector<void*> ptrs;
  ptrs.reserve(kIterations);
  
  for (int i = 0; i < kIterations; ++i) {
    ptrs.push_back(malloc(100));
  }
  
  for (auto* ptr : ptrs) {
    free(ptr);
  }
  
  end = std::chrono::steady_clock::now();
  auto malloc_duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  
  std::cout << "Arena: " << arena_duration.count() << " us\n";
  std::cout << "Malloc: " << malloc_duration.count() << " us\n";
  
  // Arena should be significantly faster
  EXPECT_LT(arena_duration.count(), malloc_duration.count());
}

} // namespace astra::core::memory