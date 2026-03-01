#include <gtest/gtest.h>
#include "astra/container/dash_map.hpp"
#include <thread>
#include <vector>

namespace astra::container {

TEST(DashMapTest, BasicOperations) {
  DashMap<std::string, int> map(4);
  
  // Test Insert
  EXPECT_TRUE(map.Insert("key1", 100));
  EXPECT_FALSE(map.Insert("key1", 200));  // Update existing
  
  // Test Get
  int value;
  EXPECT_TRUE(map.Get("key1", &value));
  EXPECT_EQ(value, 200);
  
  EXPECT_FALSE(map.Get("nonexistent", &value));
  
  // Test Contains
  EXPECT_TRUE(map.Contains("key1"));
  EXPECT_FALSE(map.Contains("nonexistent"));
  
  // Test Size
  EXPECT_EQ(map.Size(), 1);
  
  // Test Remove
  EXPECT_TRUE(map.Remove("key1"));
  EXPECT_FALSE(map.Remove("key1"));  // Already removed
  EXPECT_EQ(map.Size(), 0);
}

TEST(DashMapTest, StringViewLookup) {
  StringMap map(4);
  
  map.Insert("hello", "world");
  
  std::string value;
  EXPECT_TRUE(map.Get(absl::string_view("hello"), &value));
  EXPECT_EQ(value, "world");
  
  EXPECT_TRUE(map.Contains(absl::string_view("hello")));
  EXPECT_TRUE(map.Remove(absl::string_view("hello")));
}

TEST(DashMapTest, MultipleKeys) {
  DashMap<int, std::string> map(4);
  
  for (int i = 0; i < 100; ++i) {
    map.Insert(i, "value_" + std::to_string(i));
  }
  
  EXPECT_EQ(map.Size(), 100);
  
  std::string value;
  for (int i = 0; i < 100; ++i) {
    EXPECT_TRUE(map.Get(i, &value));
    EXPECT_EQ(value, "value_" + std::to_string(i));
  }
}

TEST(DashMapTest, ConcurrentWrites) {
  DashMap<int, int> map(16);
  const int num_threads = 10;
  const int keys_per_thread = 1000;
  
  std::vector<std::thread> threads;
  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([&map, t, keys_per_thread]() {
      int start = t * keys_per_thread;
      for (int i = 0; i < keys_per_thread; ++i) {
        int key = start + i;
        map.Insert(key, key * 2);
      }
    });
  }
  
  for (auto& thread : threads) {
    thread.join();
  }
  
  EXPECT_EQ(map.Size(), num_threads * keys_per_thread);
  
  int value;
  for (int t = 0; t < num_threads; ++t) {
    int start = t * keys_per_thread;
    for (int i = 0; i < keys_per_thread; ++i) {
      int key = start + i;
      EXPECT_TRUE(map.Get(key, &value));
      EXPECT_EQ(value, key * 2);
    }
  }
}

TEST(DashMapTest, ConcurrentReadsAndWrites) {
  StringMap map(16);
  
  // Pre-populate
  for (int i = 0; i < 100; ++i) {
    map.Insert("key" + std::to_string(i), "value" + std::to_string(i));
  }
  
  std::atomic<bool> stop{false};
  std::vector<std::thread> readers;
  std::vector<std::thread> writers;
  
  // Reader threads
  for (int t = 0; t < 5; ++t) {
    readers.emplace_back([&map, &stop]() {
      while (!stop.load()) {
        std::string value;
        for (int i = 0; i < 100; ++i) {
          map.Get("key" + std::to_string(i), &value);
        }
      }
    });
  }
  
  // Writer threads
  for (int t = 0; t < 5; ++t) {
    writers.emplace_back([&map, &stop]() {
      while (!stop.load()) {
        for (int i = 0; i < 100; ++i) {
          map.Insert("key" + std::to_string(i), "updated" + std::to_string(i));
        }
      }
    });
  }
  
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  stop.store(true);
  
  for (auto& thread : readers) {
    thread.join();
  }
  for (auto& thread : writers) {
    thread.join();
  }
}

TEST(DashSetTest, BasicOperations) {
  DashSet<std::string> set(4);
  
  EXPECT_TRUE(set.Insert("item1"));
  EXPECT_FALSE(set.Insert("item1"));  // Duplicate
  
  EXPECT_TRUE(set.Contains("item1"));
  EXPECT_FALSE(set.Contains("nonexistent"));
  
  EXPECT_TRUE(set.Remove("item1"));
  EXPECT_FALSE(set.Remove("item1"));  // Already removed
  
  EXPECT_EQ(set.Size(), 0);
}

TEST(DashMapTest, Clear) {
  DashMap<int, int> map(4);
  
  for (int i = 0; i < 100; ++i) {
    map.Insert(i, i);
  }
  
  EXPECT_EQ(map.Size(), 100);
  map.Clear();
  EXPECT_EQ(map.Size(), 0);
  EXPECT_TRUE(map.Empty());
}

}  // namespace astra::container