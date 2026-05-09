#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>

#include "astra/base/config.hpp"
#include "astra/commands/database.hpp"
#include "astra/persistence/rocksdb_adapter.hpp"

namespace astra::persistence {
namespace {

std::filesystem::path MakeTempDbPath(const std::string& test_name) {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("astradb_" + test_name + "_" + std::to_string(nonce));
}

class RocksDBStorageModeTest : public ::testing::Test {
 protected:
  void SetUp() override { db_path_ = MakeTempDbPath("storage_mode"); }

  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(db_path_, ec);
  }

  RocksDBAdapter::Config MakeConfig() const {
    RocksDBAdapter::Config config;
    config.db_path = db_path_.string();
    config.create_if_missing = true;
    config.enable_wal = true;
    config.enable_compression = false;
    return config;
  }

  std::filesystem::path db_path_;
};

TEST_F(RocksDBStorageModeTest, RocksDBAllInModePersistsStringSet) {
  constexpr const char* kKey = "rocksdb-all-in-key";
  constexpr const char* kValue = "rocksdb-all-in-value";

  {
    RocksDBAdapter adapter(MakeConfig());
    ASSERT_TRUE(adapter.IsOpen());

    commands::Database db;
    db.SetRocksDBAdapter(&adapter, base::StorageMode::kRocksDB);
    ASSERT_TRUE(db.Set(kKey, kValue));
    ASSERT_TRUE(adapter.Flush());
  }

  {
    RocksDBAdapter adapter(MakeConfig());
    ASSERT_TRUE(adapter.IsOpen());

    commands::Database db;
    db.SetRocksDBAdapter(&adapter, base::StorageMode::kRocksDB);
    auto value = db.Get(kKey);

    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value->value, kValue);
  }
}

TEST_F(RocksDBStorageModeTest, RedisModeDoesNotPersistPlainSetImmediately) {
  constexpr const char* kKey = "redis-mode-key";
  constexpr const char* kValue = "redis-mode-value";

  {
    RocksDBAdapter adapter(MakeConfig());
    ASSERT_TRUE(adapter.IsOpen());

    commands::Database db;
    db.SetRocksDBAdapter(&adapter, base::StorageMode::kRedis);
    ASSERT_TRUE(db.Set(kKey, kValue));
    ASSERT_TRUE(adapter.Flush());
  }

  {
    RocksDBAdapter adapter(MakeConfig());
    ASSERT_TRUE(adapter.IsOpen());

    commands::Database db;
    db.SetRocksDBAdapter(&adapter, base::StorageMode::kRedis);
    EXPECT_FALSE(db.Get(kKey).has_value());
  }
}

}  // namespace
}  // namespace astra::persistence
