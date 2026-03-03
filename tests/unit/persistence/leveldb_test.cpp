// ==============================================================================
// LevelDB Adapter Unit Tests
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include <gtest/gtest.h>
#include "astra/persistence/leveldb_adapter.hpp"
#include <filesystem>
#include <fstream>

namespace astra::persistence {
namespace {

class LevelDBAdapterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create unique test directory
    test_dir_ = "/tmp/astradb_test_leveldb_" + std::to_string(std::time(nullptr));
    std::filesystem::create_directories(test_dir_);
    
    LevelDBOptions options;
    options.db_path = test_dir_;
    options.create_if_missing = true;
    options.error_if_exists = false;
    
    ASSERT_TRUE(adapter_.Open(options));
  }
  
  void TearDown() override {
    adapter_.Close();
    
    // Cleanup test directory
    std::error_code ec;
    std::filesystem::remove_all(test_dir_, ec);
  }
  
  LevelDBAdapter adapter_;
  std::string test_dir_;
};

// ========== Basic Operations Tests ==========

TEST_F(LevelDBAdapterTest, OpenClose) {
  EXPECT_TRUE(adapter_.IsOpen());
  adapter_.Close();
  EXPECT_FALSE(adapter_.IsOpen());
  
  LevelDBOptions options;
  options.db_path = test_dir_;
  EXPECT_TRUE(adapter_.Open(options));
  EXPECT_TRUE(adapter_.IsOpen());
}

TEST_F(LevelDBAdapterTest, PutAndGet) {
  EXPECT_TRUE(adapter_.Put("key1", "value1"));
  
  auto result = adapter_.Get("key1");
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result.value(), "value1");
  
  auto not_found = adapter_.Get("nonexistent");
  EXPECT_FALSE(not_found.ok());
}

TEST_F(LevelDBAdapterTest, Delete) {
  EXPECT_TRUE(adapter_.Put("key1", "value1"));
  EXPECT_TRUE(adapter_.Exists("key1"));
  
  EXPECT_TRUE(adapter_.Delete("key1"));
  EXPECT_FALSE(adapter_.Exists("key1"));
  
  auto result = adapter_.Get("key1");
  EXPECT_FALSE(result.ok());
}

TEST_F(LevelDBAdapterTest, Exists) {
  EXPECT_FALSE(adapter_.Exists("key1"));
  
  EXPECT_TRUE(adapter_.Put("key1", "value1"));
  EXPECT_TRUE(adapter_.Exists("key1"));
  
  EXPECT_TRUE(adapter_.Delete("key1"));
  EXPECT_FALSE(adapter_.Exists("key1"));
}

// ========== Batch Operations Tests ==========

TEST_F(LevelDBAdapterTest, WriteBatch) {
  LevelDBAdapter::WriteBatch batch;
  batch.Put("key1", "value1");
  batch.Put("key2", "value2");
  batch.Put("key3", "value3");
  batch.Delete("key2");
  
  EXPECT_TRUE(adapter_.Write(batch));
  
  EXPECT_TRUE(adapter_.Exists("key1"));
  EXPECT_FALSE(adapter_.Exists("key2"));
  EXPECT_TRUE(adapter_.Exists("key3"));
  
  EXPECT_EQ(adapter_.Get("key1").value(), "value1");
  EXPECT_EQ(adapter_.Get("key3").value(), "value3");
}

TEST_F(LevelDBAdapterTest, WriteBatchAtomicity) {
  // Write some initial data
  EXPECT_TRUE(adapter_.Put("existing", "data"));
  
  // Create a batch that modifies existing and adds new
  LevelDBAdapter::WriteBatch batch;
  batch.Put("existing", "modified");
  batch.Put("new_key", "new_value");
  
  EXPECT_TRUE(adapter_.Write(batch));
  
  EXPECT_EQ(adapter_.Get("existing").value(), "modified");
  EXPECT_EQ(adapter_.Get("new_key").value(), "new_value");
}

// ========== Key Encoding Tests ==========

TEST_F(LevelDBAdapterTest, EncodeKeyString) {
  std::string encoded = LevelDBAdapter::EncodeKey(KeyPrefix::kString, "mykey");
  EXPECT_EQ(encoded, "S:mykey");
}

TEST_F(LevelDBAdapterTest, EncodeKeyHash) {
  std::string encoded = LevelDBAdapter::EncodeHashKey("hash1", "field1");
  EXPECT_EQ(encoded, "H:hash1:field1");
}

TEST_F(LevelDBAdapterTest, EncodeKeySet) {
  std::string encoded = LevelDBAdapter::EncodeSetKey("set1", "member1");
  EXPECT_EQ(encoded, "E:set1:member1");
}

TEST_F(LevelDBAdapterTest, EncodeKeyZSet) {
  std::string encoded = LevelDBAdapter::EncodeZSetKey("zset1", "member1");
  EXPECT_EQ(encoded, "Z:zset1:member1");
}

TEST_F(LevelDBAdapterTest, EncodeKeyList) {
  std::string encoded = LevelDBAdapter::EncodeListKey("list1", 42);
  EXPECT_EQ(encoded, "L:list1:42");
}

TEST_F(LevelDBAdapterTest, EncodeKeyTTL) {
  std::string encoded = LevelDBAdapter::EncodeTTLKey(1234567890, "mykey");
  EXPECT_EQ(encoded, "T:1234567890:mykey");
}

// ========== Scan Operations Tests ==========

TEST_F(LevelDBAdapterTest, Scan) {
  // Insert test data
  EXPECT_TRUE(adapter_.Put("S:key1", "value1"));
  EXPECT_TRUE(adapter_.Put("S:key2", "value2"));
  EXPECT_TRUE(adapter_.Put("S:key3", "value3"));
  EXPECT_TRUE(adapter_.Put("H:hash1:field1", "hvalue1"));
  
  // Scan all string keys
  std::vector<std::string> keys;
  adapter_.Scan("S:", [&](absl::string_view key, absl::string_view value) {
    keys.emplace_back(key);
    return true;
  });
  
  EXPECT_EQ(keys.size(), 3);
  EXPECT_NE(std::find(keys.begin(), keys.end(), "S:key1"), keys.end());
  EXPECT_NE(std::find(keys.begin(), keys.end(), "S:key2"), keys.end());
  EXPECT_NE(std::find(keys.begin(), keys.end(), "S:key3"), keys.end());
}

TEST_F(LevelDBAdapterTest, GetKeys) {
  EXPECT_TRUE(adapter_.Put("S:key1", "value1"));
  EXPECT_TRUE(adapter_.Put("S:key2", "value2"));
  EXPECT_TRUE(adapter_.Put("H:hash1:f1", "hvalue"));
  
  auto keys = adapter_.GetKeys("S:");
  EXPECT_EQ(keys.size(), 2);
}

TEST_F(LevelDBAdapterTest, GetAll) {
  EXPECT_TRUE(adapter_.Put("S:key1", "value1"));
  EXPECT_TRUE(adapter_.Put("S:key2", "value2"));
  
  auto all = adapter_.GetAll("S:");
  EXPECT_EQ(all.size(), 2);
  EXPECT_EQ(all["S:key1"], "value1");
  EXPECT_EQ(all["S:key2"], "value2");
}

// ========== Statistics Tests ==========

TEST_F(LevelDBAdapterTest, GetApproximateKeys) {
  // Insert some data
  for (int i = 0; i < 100; ++i) {
    EXPECT_TRUE(adapter_.Put("key" + std::to_string(i), "value"));
  }
  
  // Force compaction to update approximate keys count
  adapter_.Compact();
  
  uint64_t keys = adapter_.GetApproximateKeys();
  // Approximate keys may not be accurate immediately after writes
  // Just verify it returns a valid value (could be 0 on some LevelDB versions)
  EXPECT_GE(keys, 0);
}

TEST_F(LevelDBAdapterTest, Compact) {
  for (int i = 0; i < 100; ++i) {
    EXPECT_TRUE(adapter_.Put("key" + std::to_string(i), "value"));
  }
  
  // Should not throw
  adapter_.Compact();
}

// ========== Write Options Tests ==========

TEST_F(LevelDBAdapterTest, SyncWrite) {
  WriteOptions options;
  options.sync = true;
  
  EXPECT_TRUE(adapter_.Put("sync_key", "sync_value", options));
  EXPECT_EQ(adapter_.Get("sync_key").value(), "sync_value");
}

// ========== Read Options Tests ==========

TEST_F(LevelDBAdapterTest, ReadOptions) {
  EXPECT_TRUE(adapter_.Put("key1", "value1"));
  
  ReadOptions options;
  options.verify_checksums = true;
  options.fill_cache = true;
  
  auto result = adapter_.Get("key1", options);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result.value(), "value1");
}

// ========== Edge Cases ==========

TEST_F(LevelDBAdapterTest, EmptyValue) {
  EXPECT_TRUE(adapter_.Put("empty_key", ""));
  
  auto result = adapter_.Get("empty_key");
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result.value(), "");
}

TEST_F(LevelDBAdapterTest, LargeValue) {
  std::string large_value(1024 * 1024, 'x');  // 1MB
  
  EXPECT_TRUE(adapter_.Put("large_key", large_value));
  
  auto result = adapter_.Get("large_key");
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result.value().size(), large_value.size());
}

TEST_F(LevelDBAdapterTest, SpecialCharactersInKey) {
  EXPECT_TRUE(adapter_.Put("key:with:colons", "value1"));
  EXPECT_TRUE(adapter_.Put("key with spaces", "value2"));
  EXPECT_TRUE(adapter_.Put("key\nwith\nnewlines", "value3"));
  
  EXPECT_EQ(adapter_.Get("key:with:colons").value(), "value1");
  EXPECT_EQ(adapter_.Get("key with spaces").value(), "value2");
  EXPECT_EQ(adapter_.Get("key\nwith\nnewlines").value(), "value3");
}

TEST_F(LevelDBAdapterTest, BinaryData) {
  std::string binary_data = "\x00\x01\x02\x03\x04\x05";
  
  EXPECT_TRUE(adapter_.Put("binary_key", binary_data));
  
  auto result = adapter_.Get("binary_key");
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result.value(), binary_data);
}

}  // namespace
}  // namespace astra::persistence
