// ==============================================================================
// AOF FlatBuffers Tests
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "astra/persistence/aof_flatbuffers.hpp"
#include "astra/commands/database.hpp"

namespace astra::persistence {
namespace test {

// Test fixture for AOF FlatBuffers
class AofFlatbuffersTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Initialize test database
    test_db_ = std::make_unique<commands::Database>();
  }
  
  std::unique_ptr<commands::Database> test_db_;
};

// Test SET command serialization
TEST_F(AofFlatbuffersTest, SerializeSetCommand) {
  std::string key = "test_key";
  std::string value = "test_value";
  
  auto data = AofFlatbuffersSerializer::SerializeSetCommand(0, key, value);
  
  ASSERT_FALSE(data.empty());
  EXPECT_GT(data.size(), 0);
}

// Test GET command serialization
TEST_F(AofFlatbuffersTest, SerializeGetCommand) {
  std::string key = "test_key";
  
  auto data = AofFlatbuffersSerializer::SerializeGetCommand(0, key);
  
  ASSERT_FALSE(data.empty());
  EXPECT_GT(data.size(), 0);
}

// Test DEL command serialization
TEST_F(AofFlatbuffersTest, SerializeDelCommand) {
  std::string key = "test_key";
  
  auto data = AofFlatbuffersSerializer::SerializeDelCommand(0, key);
  
  ASSERT_FALSE(data.empty());
  EXPECT_GT(data.size(), 0);
}

// Test EXPIRE command serialization
TEST_F(AofFlatbuffersTest, SerializeExpireCommand) {
  std::string key = "test_key";
  int64_t ttl_ms = 60000;  // 60 seconds
  
  auto data = AofFlatbuffersSerializer::SerializeExpireCommand(0, key, ttl_ms);
  
  ASSERT_FALSE(data.empty());
  EXPECT_GT(data.size(), 0);
}

// Test empty data handling
TEST_F(AofFlatbuffersTest, HandleEmptyData) {
  AofCommandType type;
  std::string key;
  
  bool success = AofFlatbuffersSerializer::DeserializeCommand(nullptr, 0, type, key);
  EXPECT_FALSE(success);
  
  std::vector<uint8_t> empty_data;
  success = AofFlatbuffersSerializer::DeserializeCommand(empty_data.data(), empty_data.size(), type, key);
  EXPECT_FALSE(success);
}

// Test invalid data handling
TEST_F(AofFlatbuffersTest, HandleInvalidData) {
  std::vector<uint8_t> invalid_data = {0x01, 0x02, 0x03};
  
  AofCommandType type;
  std::string key;
  
  bool success = AofFlatbuffersSerializer::DeserializeCommand(invalid_data.data(), invalid_data.size(), type, key);
  EXPECT_FALSE(success);
}

}  // namespace test
}  // namespace astra::persistence
