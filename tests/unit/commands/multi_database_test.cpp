// ==============================================================================
// Multi-Database Functionality Tests
// ==============================================================================

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "astra/commands/database.hpp"
#include "astra/commands/admin_commands.hpp"
#include "astra/commands/string_commands.hpp"
#include "astra/protocol/resp/resp_parser.hpp"
#include "astra/protocol/resp/resp_builder.hpp"
#include "astra/persistence/leveldb_adapter.hpp"
#include "astra/base/version.hpp"

namespace astra::commands {
namespace test {

// Test context for multi-database operations
class TestMultiDBContext : public CommandContext {
 public:
  explicit TestMultiDBContext(DatabaseManager* db_manager)
      : db_manager_(db_manager), db_index_(0), authenticated_(true) {}

  Database* GetDatabase() const override {
    if (db_manager_) {
      return db_manager_->GetDatabase(db_index_);
    }
    return nullptr;
  }

  int GetDBIndex() const override { return db_index_; }
  void SetDBIndex(int index) override { db_index_ = index; }
  bool IsAuthenticated() const override { return authenticated_; }
  void SetAuthenticated(bool auth) override { authenticated_ = auth; }
  DatabaseManager* GetDatabaseManager() const override { return db_manager_; }

  bool IsPersistenceEnabled() const override { return false; }
  persistence::LevelDBAdapter* GetPersistence() const override { return nullptr; }

 private:
  DatabaseManager* db_manager_;
  int db_index_;
  bool authenticated_;
};

// Multi-database test fixture
class MultiDatabaseTest : public ::testing::Test {
 protected:
  void SetUp() override {
    db_manager_ = std::make_unique<DatabaseManager>(16);
    context_ = std::make_unique<TestMultiDBContext>(db_manager_.get());
  }

  void TearDown() override {
    context_.reset();
    db_manager_.reset();
  }

  Command CreateCommand(const std::string& cmd_name, const std::vector<std::string>& args) {
    Command command;
    command.name = cmd_name;
    
    // Create RESP values for arguments
    for (const auto& arg : args) {
      RespValue arg_value;
      arg_value.SetString(arg, astra::protocol::RespType::kBulkString);
      command.args.push_back(arg_value);
    }
    
    return command;
  }

  std::unique_ptr<DatabaseManager> db_manager_;
  std::unique_ptr<TestMultiDBContext> context_;
};

// Test SELECT command
TEST_F(MultiDatabaseTest, SelectCommand) {
  // Test switching to different databases
  for (int i = 0; i < 16; ++i) {
    auto cmd = CreateCommand("SELECT", {std::to_string(i)});
    auto result = HandleSelect(cmd, context_.get());
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(context_->GetDBIndex(), i);
  }

  // Test invalid database index (negative)
  {
    auto cmd = CreateCommand("SELECT", {"-1"});
    auto result = HandleSelect(cmd, context_.get());
    EXPECT_FALSE(result.success);
    EXPECT_THAT(result.error, testing::HasSubstr("out of range"));
  }

  // Test invalid database index (too large)
  {
    auto cmd = CreateCommand("SELECT", {"16"});
    auto result = HandleSelect(cmd, context_.get());
    EXPECT_FALSE(result.success);
    EXPECT_THAT(result.error, testing::HasSubstr("out of range"));
  }

  // Test invalid argument (not a number)
  {
    auto cmd = CreateCommand("SELECT", {"abc"});
    auto result = HandleSelect(cmd, context_.get());
    EXPECT_FALSE(result.success);
    EXPECT_THAT(result.error, testing::HasSubstr("invalid"));
  }

  // Test missing argument
  {
    auto cmd = CreateCommand("SELECT", {});
    auto result = HandleSelect(cmd, context_.get());
    EXPECT_FALSE(result.success);
    EXPECT_THAT(result.error, testing::HasSubstr("wrong number"));
  }
}

// Test SELECT command with data isolation
TEST_F(MultiDatabaseTest, SelectWithDataIsolation) {
  // Set key in database 0
  context_->SetDBIndex(0);
  auto* db0 = context_->GetDatabase();
  db0->Set("key", "value_in_db0");

  // Set key in database 1
  context_->SetDBIndex(1);
  auto* db1 = context_->GetDatabase();
  db1->Set("key", "value_in_db1");

  // Verify data isolation
  context_->SetDBIndex(0);
  auto val0 = db0->Get("key");
  ASSERT_TRUE(val0.has_value());
  EXPECT_EQ(val0.value().value, "value_in_db0");

  context_->SetDBIndex(1);
  auto val1 = db1->Get("key");
  ASSERT_TRUE(val1.has_value());
  EXPECT_EQ(val1.value().value, "value_in_db1");

  // Verify key doesn't exist in other databases
  EXPECT_FALSE(db0->Get("key_in_db1").has_value());
  EXPECT_FALSE(db1->Get("key_in_db0").has_value());
}

// Test FLUSHDB command (clears only current database)
TEST_F(MultiDatabaseTest, FlushDBCommand) {
  // Set keys in database 0 and 1
  context_->SetDBIndex(0);
  auto* db0 = context_->GetDatabase();
  db0->Set("key0", "value0");

  context_->SetDBIndex(1);
  auto* db1 = context_->GetDatabase();
  db1->Set("key1", "value1");

  // Flush database 0
  context_->SetDBIndex(0);
  auto cmd = CreateCommand("FLUSHDB", {});
  auto result = HandleFlushDb(cmd, context_.get());
  EXPECT_TRUE(result.success);

  // Verify database 0 is cleared
  EXPECT_FALSE(db0->Get("key0").has_value());

  // Verify database 1 is not affected
  auto val1 = db1->Get("key1");
  ASSERT_TRUE(val1.has_value());
  EXPECT_EQ(val1.value().value, "value1");
}

// Test FLUSHALL command (clears all databases)
TEST_F(MultiDatabaseTest, FlushAllCommand) {
  // Set keys in multiple databases
  for (int i = 0; i < 3; ++i) {
    context_->SetDBIndex(i);
    auto* db = context_->GetDatabase();
    db->Set("key" + std::to_string(i), "value" + std::to_string(i));
  }

  // Flush all databases
  auto cmd = CreateCommand("FLUSHALL", {});
  auto result = HandleFlushAll(cmd, context_.get());
  EXPECT_TRUE(result.success);

  // Verify all databases are cleared
  for (int i = 0; i < 3; ++i) {
    context_->SetDBIndex(i);
    auto* db = context_->GetDatabase();
    EXPECT_FALSE(db->Get("key" + std::to_string(i)).has_value());
  }
}

// Test DBSIZE command across databases
TEST_F(MultiDatabaseTest, DBSizeCommand) {
  // Set keys in database 0
  context_->SetDBIndex(0);
  auto* db0 = context_->GetDatabase();
  db0->Set("key1", "value1");
  db0->Set("key2", "value2");

  // Set keys in database 1
  context_->SetDBIndex(1);
  auto* db1 = context_->GetDatabase();
  db1->Set("key3", "value3");

  // Check DBSIZE for database 0
  context_->SetDBIndex(0);
  auto cmd0 = CreateCommand("DBSIZE", {});
  auto result0 = HandleDbSize(cmd0, context_.get());
  EXPECT_TRUE(result0.success);
  EXPECT_EQ(result0.response.AsInteger(), 2);

  // Check DBSIZE for database 1
  context_->SetDBIndex(1);
  auto cmd1 = CreateCommand("DBSIZE", {});
  auto result1 = HandleDbSize(cmd1, context_.get());
  EXPECT_TRUE(result1.success);
  EXPECT_EQ(result1.response.AsInteger(), 1);

  // Check DBSIZE for empty database
  context_->SetDBIndex(2);
  auto cmd2 = CreateCommand("DBSIZE", {});
  auto result2 = HandleDbSize(cmd2, context_.get());
  EXPECT_TRUE(result2.success);
  EXPECT_EQ(result2.response.AsInteger(), 0);
}

// Test DatabaseManager basic functionality
TEST_F(MultiDatabaseTest, DatabaseManagerBasics) {
  // Test getting database by index
  for (int i = 0; i < 16; ++i) {
    auto* db = db_manager_->GetDatabase(i);
    ASSERT_NE(db, nullptr);
  }

  // Test getting invalid database index
  auto* db_invalid = db_manager_->GetDatabase(16);
  EXPECT_EQ(db_invalid, nullptr);

  auto* db_negative = db_manager_->GetDatabase(-1);
  EXPECT_EQ(db_negative, nullptr);

  // Test database count
  EXPECT_EQ(db_manager_->GetDatabaseCount(), 16);
}

// Test version header
TEST_F(MultiDatabaseTest, VersionHeader) {
  // Test version constants
  EXPECT_EQ(ASTRADB_VERSION_MAJOR, 1);
  EXPECT_EQ(ASTRADB_VERSION_MINOR, 0);
  EXPECT_EQ(ASTRADB_VERSION_PATCH, 0);
  EXPECT_STREQ(ASTRADB_VERSION, "1.0.0");

  // Test git information
  EXPECT_STREQ(ASTRADB_GIT_BRANCH, "main");
  EXPECT_NE(std::string(ASTRADB_GIT_COMMIT_SHORT), "unknown");
  EXPECT_NE(std::string(ASTRADB_GIT_COMMIT_SHORT), "");

  // Test VersionInfo struct
  EXPECT_EQ(base::kVersion.major, 1);
  EXPECT_EQ(base::kVersion.minor, 0);
  EXPECT_EQ(base::kVersion.patch, 0);
  EXPECT_STREQ(base::kVersion.version, "1.0.0");
  EXPECT_STREQ(base::kVersion.GetFullVersion(), "1.0.0");
  EXPECT_STREQ(base::kVersion.GetGitBranch(), "main");
  EXPECT_STREQ(base::kVersion.GetGitCommitShort(), ASTRADB_GIT_COMMIT_SHORT);
}

}  // namespace test
}  // namespace astra::commands