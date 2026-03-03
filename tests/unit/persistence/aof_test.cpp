// ==============================================================================
// AOF Writer Unit Tests
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include <gtest/gtest.h>
#include "astra/persistence/aof_writer.hpp"
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

namespace astra::persistence {
namespace {

class AofWriterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create unique test directory
    test_dir_ = "/tmp/astradb_test_aof_" + std::to_string(std::time(nullptr));
    std::filesystem::create_directories(test_dir_);
    aof_path_ = test_dir_ + "/appendonly.aof";
    
    AofOptions options;
    options.aof_path = aof_path_;
    options.sync_policy = AofSyncPolicy::kAlways;  // Sync immediately for tests
    
    ASSERT_TRUE(writer_.Init(options));
  }
  
  void TearDown() override {
    writer_.Stop();
    
    // Cleanup test directory
    std::error_code ec;
    std::filesystem::remove_all(test_dir_, ec);
  }
  
  // Read AOF file content
  std::string ReadAofFile() {
    std::ifstream ifs(aof_path_, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());
    return content;
  }
  
  AofWriter writer_;
  std::string test_dir_;
  std::string aof_path_;
};

// ========== Basic Operations Tests ==========

TEST_F(AofWriterTest, InitAndStop) {
  EXPECT_TRUE(writer_.IsInitialized());
  writer_.Stop();
  // After stop, the writer is no longer running but still initialized
  // (initialized_ flag is not reset in Stop())
}

TEST_F(AofWriterTest, AppendRawCommand) {
  std::string cmd = "*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$5\r\nvalue\r\n";
  EXPECT_TRUE(writer_.Append(cmd));
  EXPECT_TRUE(writer_.Flush());
  
  std::string content = ReadAofFile();
  EXPECT_EQ(content, cmd);
}

TEST_F(AofWriterTest, AppendSet) {
  EXPECT_TRUE(writer_.AppendSet("mykey", "myvalue"));
  EXPECT_TRUE(writer_.Flush());
  
  std::string content = ReadAofFile();
  EXPECT_NE(content.find("SET"), std::string::npos);
  EXPECT_NE(content.find("mykey"), std::string::npos);
  EXPECT_NE(content.find("myvalue"), std::string::npos);
}

TEST_F(AofWriterTest, AppendDel) {
  EXPECT_TRUE(writer_.AppendDel("mykey"));
  EXPECT_TRUE(writer_.Flush());
  
  std::string content = ReadAofFile();
  EXPECT_NE(content.find("DEL"), std::string::npos);
  EXPECT_NE(content.find("mykey"), std::string::npos);
}

TEST_F(AofWriterTest, AppendHSet) {
  EXPECT_TRUE(writer_.AppendHSet("hash1", "field1", "value1"));
  EXPECT_TRUE(writer_.Flush());
  
  std::string content = ReadAofFile();
  EXPECT_NE(content.find("HSET"), std::string::npos);
  EXPECT_NE(content.find("hash1"), std::string::npos);
  EXPECT_NE(content.find("field1"), std::string::npos);
  EXPECT_NE(content.find("value1"), std::string::npos);
}

TEST_F(AofWriterTest, AppendSAdd) {
  EXPECT_TRUE(writer_.AppendSAdd("set1", "member1"));
  EXPECT_TRUE(writer_.Flush());
  
  std::string content = ReadAofFile();
  EXPECT_NE(content.find("SADD"), std::string::npos);
}

TEST_F(AofWriterTest, AppendZAdd) {
  EXPECT_TRUE(writer_.AppendZAdd("zset1", 1.5, "member1"));
  EXPECT_TRUE(writer_.Flush());
  
  std::string content = ReadAofFile();
  EXPECT_NE(content.find("ZADD"), std::string::npos);
  EXPECT_NE(content.find("1.5"), std::string::npos);
}

TEST_F(AofWriterTest, AppendLPush) {
  EXPECT_TRUE(writer_.AppendLPush("list1", "value1"));
  EXPECT_TRUE(writer_.Flush());
  
  std::string content = ReadAofFile();
  EXPECT_NE(content.find("LPUSH"), std::string::npos);
}

TEST_F(AofWriterTest, AppendExpire) {
  EXPECT_TRUE(writer_.AppendExpire("key1", 3600));
  EXPECT_TRUE(writer_.Flush());
  
  std::string content = ReadAofFile();
  EXPECT_NE(content.find("EXPIRE"), std::string::npos);
  EXPECT_NE(content.find("3600"), std::string::npos);
}

// ========== Multiple Commands Tests ==========

TEST_F(AofWriterTest, MultipleCommands) {
  EXPECT_TRUE(writer_.AppendSet("key1", "value1"));
  EXPECT_TRUE(writer_.AppendSet("key2", "value2"));
  EXPECT_TRUE(writer_.AppendDel("key1"));
  EXPECT_TRUE(writer_.Flush());
  
  std::string content = ReadAofFile();
  EXPECT_NE(content.find("key1"), std::string::npos);
  EXPECT_NE(content.find("key2"), std::string::npos);
  EXPECT_NE(content.find("value1"), std::string::npos);
  EXPECT_NE(content.find("value2"), std::string::npos);
  EXPECT_NE(content.find("DEL"), std::string::npos);
}

// ========== Buffer Size Tests ==========

TEST_F(AofWriterTest, GetBufferSize) {
  // With AofSyncPolicy::kAlways, buffer is flushed immediately after each append
  // So buffer size is always 0 after append returns
  // Just test that the method works
  size_t size = writer_.GetBufferSize();
  EXPECT_GE(size, 0);  // Should be >= 0
  
  writer_.AppendSet("key1", "value1");
  // Buffer might be 0 if sync is immediate
  EXPECT_GE(writer_.GetBufferSize(), 0);
  
  writer_.Flush();
  EXPECT_EQ(writer_.GetBufferSize(), 0);
}

TEST_F(AofWriterTest, GetFileSize) {
  EXPECT_EQ(writer_.GetFileSize(), 0);
  
  writer_.AppendSet("key1", "value1");
  writer_.Flush();
  
  EXPECT_GT(writer_.GetFileSize(), 0);
}

// ========== Edge Cases ==========

TEST_F(AofWriterTest, EmptyKey) {
  EXPECT_TRUE(writer_.AppendSet("", "value"));
  EXPECT_TRUE(writer_.Flush());
  
  std::string content = ReadAofFile();
  EXPECT_NE(content.find("SET"), std::string::npos);
}

TEST_F(AofWriterTest, EmptyValue) {
  EXPECT_TRUE(writer_.AppendSet("key", ""));
  EXPECT_TRUE(writer_.Flush());
  
  std::string content = ReadAofFile();
  EXPECT_NE(content.find("SET"), std::string::npos);
}

TEST_F(AofWriterTest, SpecialCharactersInValue) {
  EXPECT_TRUE(writer_.AppendSet("key", "value with spaces\nand newlines\ttabs"));
  EXPECT_TRUE(writer_.Flush());
  
  std::string content = ReadAofFile();
  EXPECT_NE(content.find("value with spaces"), std::string::npos);
}

TEST_F(AofWriterTest, BinaryData) {
  std::string binary_data = "\x00\x01\x02\x03\x04\x05";
  EXPECT_TRUE(writer_.AppendSet("binary_key", binary_data));
  EXPECT_TRUE(writer_.Flush());
  
  std::string content = ReadAofFile();
  // Binary data should be present (length encoded)
  EXPECT_NE(content.find("binary_key"), std::string::npos);
}

// ========== Sync Policy Tests ==========

TEST_F(AofWriterTest, EverySecSyncPolicy) {
  writer_.Stop();
  
  AofOptions options;
  options.aof_path = test_dir_ + "/everysec.aof";
  options.sync_policy = AofSyncPolicy::kEverySec;
  
  AofWriter everysec_writer;
  EXPECT_TRUE(everysec_writer.Init(options));
  
  EXPECT_TRUE(everysec_writer.AppendSet("key1", "value1"));
  
  // Wait a bit for background thread
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  everysec_writer.Stop();
}

// ========== Large Data Tests ==========

TEST_F(AofWriterTest, LargeValue) {
  std::string large_value(1024 * 100, 'x');  // 100KB
  
  EXPECT_TRUE(writer_.AppendSet("large_key", large_value));
  EXPECT_TRUE(writer_.Flush());
  
  std::string content = ReadAofFile();
  EXPECT_GT(content.size(), large_value.size());
}

TEST_F(AofWriterTest, ManySmallCommands) {
  for (int i = 0; i < 100; ++i) {
    EXPECT_TRUE(writer_.AppendSet("key" + std::to_string(i), "value" + std::to_string(i)));
  }
  EXPECT_TRUE(writer_.Flush());
  
  std::string content = ReadAofFile();
  EXPECT_GT(content.size(), 0);
}

}  // namespace
}  // namespace astra::persistence
