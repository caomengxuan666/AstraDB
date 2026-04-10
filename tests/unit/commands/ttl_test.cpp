// ==============================================================================
// TTL Commands Unit Tests
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "astra/commands/database.hpp"
#include "astra/commands/redis/ttl_commands.hpp"

namespace astra::commands {

class TTLCommandsTest : public ::testing::Test {
 protected:
  void SetUp() override { db_ = std::make_unique<Database>(); }

  std::unique_ptr<Database> db_;
};

TEST_F(TTLCommandsTest, SetAndGetTTL) {
  // Set a key
  db_->Set("test_key", "test_value");

  // Set expiration
  bool result = db_->SetExpireSeconds("test_key", 10);
  EXPECT_TRUE(result);

  // Check TTL
  int64_t ttl = db_->GetTtlSeconds("test_key");
  EXPECT_GT(ttl, 0);
  EXPECT_LE(ttl, 10);
}

TEST_F(TTLCommandsTest, ExpireCommand) {
  // Set a key without expiration
  db_->Set("test_key", "test_value");

  // Set expiration
  bool result = db_->SetExpireSeconds("test_key", 5);
  EXPECT_TRUE(result);

  // Check TTL
  int64_t ttl = db_->GetTtlSeconds("test_key");
  EXPECT_GT(ttl, 0);
  EXPECT_LE(ttl, 5);
}

TEST_F(TTLCommandsTest, PersistCommand) {
  // Set a key with expiration
  db_->Set("test_key", "test_value");
  db_->SetExpireSeconds("test_key", 10);

  // Remove expiration
  bool result = db_->Persist("test_key");
  EXPECT_TRUE(result);

  // Check TTL should be -1 (no expiration)
  int64_t ttl = db_->GetTtlSeconds("test_key");
  EXPECT_EQ(ttl, -1);
}

TEST_F(TTLCommandsTest, PersistNonExistentKey) {
  // Try to persist non-existent key
  bool result = db_->Persist("nonexistent_key");
  EXPECT_FALSE(result);
}

TEST_F(TTLCommandsTest, ExpireNonExistentKey) {
  // Try to set expiration on non-existent key
  bool result = db_->SetExpireSeconds("nonexistent_key", 5);
  EXPECT_FALSE(result);
}

TEST_F(TTLCommandsTest, TTLNonExistentKey) {
  // Get TTL of non-existent key
  int64_t ttl = db_->GetTtlSeconds("nonexistent_key");
  EXPECT_EQ(ttl, -2);
}

TEST_F(TTLCommandsTest, SetWithTTL) {
  // Set key then set TTL
  db_->Set("test_key", "test_value");
  db_->SetExpireSeconds("test_key", 10);

  // Verify key exists and has value
  auto value = db_->Get("test_key");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(value->value, "test_value");
}

TEST_F(TTLCommandsTest, MillisecondTTL) {
  // Set key
  db_->Set("test_key", "test_value");

  // Set expiration in milliseconds (absolute timestamp)
  int64_t expire_time_ms =
      astra::storage::KeyMetadata::GetCurrentTimeMs() + 5000;
  bool result = db_->SetExpireMs("test_key", expire_time_ms);
  EXPECT_TRUE(result);

  // Check TTL in milliseconds
  int64_t ttl_ms = db_->GetTtlMs("test_key");
  EXPECT_GT(ttl_ms, 0);
  EXPECT_LE(ttl_ms, 5000);
}

TEST_F(TTLCommandsTest, NoExpirationTTL) {
  // Set a key without expiration
  db_->Set("test_key", "test_value");

  // Check TTL should be -1 (no expiration)
  int64_t ttl = db_->GetTtlSeconds("test_key");
  EXPECT_EQ(ttl, -1);
}

TEST_F(TTLCommandsTest, KeyStillAccessibleBeforeExpiration) {
  // Set key with short TTL
  db_->Set("test_key", "test_value");
  int64_t expire_time_ms =
      astra::storage::KeyMetadata::GetCurrentTimeMs() + 1000;
  db_->SetExpireMs("test_key", expire_time_ms);  // 1 second

  // Key should be accessible immediately
  auto value = db_->Get("test_key");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(value->value, "test_value");
}

TEST_F(TTLCommandsTest, MultipleSetExpire) {
  // Set key with TTL
  db_->Set("test_key", "test_value");
  db_->SetExpireSeconds("test_key", 10);

  // Change TTL
  db_->SetExpireSeconds("test_key", 5);

  // Check TTL should be <= 5
  int64_t ttl = db_->GetTtlSeconds("test_key");
  EXPECT_GT(ttl, 0);
  EXPECT_LE(ttl, 5);
}

}  // namespace astra::commands
