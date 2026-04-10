// ==============================================================================
// Script Command Tests
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include <gtest/gtest.h>

#include "astra/commands/command_auto_register.hpp"
#include "astra/commands/command_handler.hpp"
#include "astra/commands/database.hpp"
#include "astra/commands/redis/script_commands.hpp"

namespace astra::commands {

using astra::protocol::Command;
using astra::protocol::RespValue;

// Simple test context
class ScriptTestContext : public CommandContext {
 public:
  ScriptTestContext() : db_(nullptr), db_index_(0), authenticated_(true) {}

  Database* GetDatabase() const override { return db_; }
  void SetDatabase(Database* db) { db_ = db; }

  int GetDBIndex() const override { return db_index_; }
  void SetDBIndex(int index) override { db_index_ = index; }

  bool IsAuthenticated() const override { return authenticated_; }
  void SetAuthenticated(bool auth) override { authenticated_ = auth; }

  bool IsInTransaction() const override { return false; }
  void BeginTransaction() override {}
  void QueueCommand(const protocol::Command& cmd) override {}
  absl::InlinedVector<protocol::Command, 16> GetQueuedCommands()
      const override {
    return {};
  }
  void ClearQueuedCommands() override {}
  void DiscardTransaction() override {}
  void WatchKey(const std::string& key, uint64_t version) override {}
  const absl::flat_hash_set<std::string>& GetWatchedKeys() const override {
    static absl::flat_hash_set<std::string> empty;
    return empty;
  }
  bool IsWatchedKeyModified(
      const absl::AnyInvocable<uint64_t(const std::string&) const>&)
      const override {
    return false;
  }
  void ClearWatchedKeys() override {}

 private:
  Database* db_;
  int db_index_;
  bool authenticated_;
};

// Script command tests
class ScriptCommandTest : public ::testing::Test {
 protected:
  void SetUp() override {
    db_manager_ = std::make_unique<DatabaseManager>(16);
    registry_ = std::make_unique<CommandRegistry>();
    RuntimeCommandRegistry::Instance().ApplyToRegistry(*registry_);
    context_ = std::make_unique<ScriptTestContext>();
    context_->SetDatabase(db_manager_->GetDatabase(0));

    // Clear script cache before each test
    GetGlobalScriptCache().Clear();
  }

  void TearDown() override {
    context_->GetDatabase()->Clear();
    GetGlobalScriptCache().Clear();
  }

  std::unique_ptr<DatabaseManager> db_manager_;
  std::unique_ptr<CommandRegistry> registry_;
  std::unique_ptr<ScriptTestContext> context_;
};

TEST_F(ScriptCommandTest, EVAL_SimpleReturn) {
  Command eval_cmd;
  eval_cmd.name = "EVAL";
  eval_cmd.args.emplace_back(std::string("return 'hello'"));
  eval_cmd.args.emplace_back(std::string("0"));

  auto result = registry_->Execute(eval_cmd, context_.get());
  ASSERT_TRUE(result.success) << "EVAL failed: " << result.error;
  EXPECT_TRUE(result.response.IsBulkString());
  EXPECT_EQ(result.response.AsString(), "hello");
}

TEST_F(ScriptCommandTest, EVAL_ReturnNumber) {
  Command eval_cmd;
  eval_cmd.name = "EVAL";
  eval_cmd.args.emplace_back(std::string("return 42"));
  eval_cmd.args.emplace_back(std::string("0"));

  auto result = registry_->Execute(eval_cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsInteger());
  EXPECT_EQ(result.response.AsInteger(), 42);
}

TEST_F(ScriptCommandTest, EVAL_ReturnTable) {
  Command eval_cmd;
  eval_cmd.name = "EVAL";
  eval_cmd.args.emplace_back(std::string("return {1, 2, 3, 'hello', 'world'}"));
  eval_cmd.args.emplace_back(std::string("0"));

  auto result = registry_->Execute(eval_cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsArray());

  const auto& arr = result.response.AsArray();
  ASSERT_EQ(arr.size(), 5);
  EXPECT_TRUE(arr[0].IsInteger());
  EXPECT_EQ(arr[0].AsInteger(), 1);
  EXPECT_TRUE(arr[1].IsInteger());
  EXPECT_EQ(arr[1].AsInteger(), 2);
  EXPECT_TRUE(arr[2].IsInteger());
  EXPECT_EQ(arr[2].AsInteger(), 3);
  EXPECT_TRUE(arr[3].IsBulkString());
  EXPECT_EQ(arr[3].AsString(), "hello");
  EXPECT_TRUE(arr[4].IsBulkString());
  EXPECT_EQ(arr[4].AsString(), "world");
}

TEST_F(ScriptCommandTest, EVAL_WithKeys) {
  // First set a key
  Command set_cmd;
  set_cmd.name = "SET";
  set_cmd.args.emplace_back(std::string("mykey"));
  set_cmd.args.emplace_back(std::string("myvalue"));
  registry_->Execute(set_cmd, context_.get());

  // Use Lua to read KEYS[1]
  Command eval_cmd;
  eval_cmd.name = "EVAL";
  eval_cmd.args.emplace_back(std::string("return KEYS[1]"));
  eval_cmd.args.emplace_back(std::string("1"));
  eval_cmd.args.emplace_back(std::string("mykey"));

  auto result = registry_->Execute(eval_cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsBulkString());
  EXPECT_EQ(result.response.AsString(), "mykey");
}

TEST_F(ScriptCommandTest, EVAL_WithArgs) {
  Command eval_cmd;
  eval_cmd.name = "EVAL";
  eval_cmd.args.emplace_back(std::string("return ARGV[1] .. ' ' .. ARGV[2]"));
  eval_cmd.args.emplace_back(std::string("0"));
  eval_cmd.args.emplace_back(std::string("hello"));
  eval_cmd.args.emplace_back(std::string("world"));

  auto result = registry_->Execute(eval_cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsBulkString());
  EXPECT_EQ(result.response.AsString(), "hello world");
}

TEST_F(ScriptCommandTest, EVAL_InvalidArguments) {
  Command eval_cmd;
  eval_cmd.name = "EVAL";
  // Missing script and numkeys
  auto result = registry_->Execute(eval_cmd, context_.get());
  ASSERT_FALSE(result.success);
}

TEST_F(ScriptCommandTest, EVAL_InvalidScript) {
  Command eval_cmd;
  eval_cmd.name = "EVAL";
  eval_cmd.args.emplace_back(std::string("invalid lua code {{"));
  eval_cmd.args.emplace_back(std::string("0"));

  auto result = registry_->Execute(eval_cmd, context_.get());
  ASSERT_FALSE(result.success);
  EXPECT_NE(result.error.find("ERR"), std::string::npos);
}

TEST_F(ScriptCommandTest, EVALSHA_CachedScript) {
  // First load the script using SCRIPT LOAD to get the correct SHA1
  std::string script = "return 'cached'";
  Command load_cmd;
  load_cmd.name = "SCRIPT";
  load_cmd.args.emplace_back(std::string("LOAD"));
  load_cmd.args.emplace_back(script);
  auto load_result = registry_->Execute(load_cmd, context_.get());
  ASSERT_TRUE(load_result.success);
  std::string sha1 = load_result.response.AsString();

  // Verify it's a 40-character hex string (SHA1)
  EXPECT_EQ(sha1.length(), 40);

  // Now use EVALSHA with the correct SHA1
  Command evalsha_cmd;
  evalsha_cmd.name = "EVALSHA";
  evalsha_cmd.args.emplace_back(sha1);
  evalsha_cmd.args.emplace_back(std::string("0"));

  auto result = registry_->Execute(evalsha_cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsBulkString());
  EXPECT_EQ(result.response.AsString(), "cached");
}

TEST_F(ScriptCommandTest, EVALSHA_NonExistentScript) {
  Command evalsha_cmd;
  evalsha_cmd.name = "EVALSHA";
  evalsha_cmd.args.emplace_back(
      std::string("0000000000000000000000000000000000000000"));
  evalsha_cmd.args.emplace_back(std::string("0"));

  auto result = registry_->Execute(evalsha_cmd, context_.get());
  ASSERT_FALSE(result.success);
  EXPECT_NE(result.error.find("NOSCRIPT"), std::string::npos);
}

TEST_F(ScriptCommandTest, SCRIPT_LOAD) {
  std::string script = "return 'loaded'";
  Command script_cmd;
  script_cmd.name = "SCRIPT";
  script_cmd.args.emplace_back(std::string("LOAD"));
  script_cmd.args.emplace_back(script);

  auto result = registry_->Execute(script_cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsBulkString());

  // Verify it's a 40-character hex string (SHA1)
  std::string sha1 = result.response.AsString();
  EXPECT_EQ(sha1.length(), 40);
}

TEST_F(ScriptCommandTest, SCRIPT_EXISTS) {
  // Load a script first
  Command load_cmd;
  load_cmd.name = "SCRIPT";
  load_cmd.args.emplace_back(std::string("LOAD"));
  load_cmd.args.emplace_back(std::string("return 'test'"));
  auto result = registry_->Execute(load_cmd, context_.get());
  ASSERT_TRUE(result.success);
  std::string sha1 = result.response.AsString();

  // Check if it exists
  Command exists_cmd;
  exists_cmd.name = "SCRIPT";
  exists_cmd.args.emplace_back(std::string("EXISTS"));
  exists_cmd.args.emplace_back(sha1);

  result = registry_->Execute(exists_cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsArray());

  const auto& arr = result.response.AsArray();
  ASSERT_EQ(arr.size(), 1);
  EXPECT_TRUE(arr[0].IsInteger());
  EXPECT_EQ(arr[0].AsInteger(), 1);
}

TEST_F(ScriptCommandTest, SCRIPT_EXISTS_NonExistent) {
  Command exists_cmd;
  exists_cmd.name = "SCRIPT";
  exists_cmd.args.emplace_back(std::string("EXISTS"));
  exists_cmd.args.emplace_back(
      std::string("0000000000000000000000000000000000000000"));

  auto result = registry_->Execute(exists_cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsArray());

  const auto& arr = result.response.AsArray();
  ASSERT_EQ(arr.size(), 1);
  EXPECT_TRUE(arr[0].IsInteger());
  EXPECT_EQ(arr[0].AsInteger(), 0);
}

TEST_F(ScriptCommandTest, SCRIPT_FLUSH) {
  // Load a script first
  Command load_cmd;
  load_cmd.name = "SCRIPT";
  load_cmd.args.emplace_back(std::string("LOAD"));
  load_cmd.args.emplace_back(std::string("return 'test'"));
  registry_->Execute(load_cmd, context_.get());

  // Flush the cache
  Command flush_cmd;
  flush_cmd.name = "SCRIPT";
  flush_cmd.args.emplace_back(std::string("FLUSH"));

  auto result = registry_->Execute(flush_cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsSimpleString());
  EXPECT_EQ(result.response.AsString(), "OK");

  // Verify cache is empty
  auto hashes = GetGlobalScriptCache().GetAllHashes();
  EXPECT_TRUE(hashes.empty());
}

TEST_F(ScriptCommandTest, SCRIPT_UnknownSubcommand) {
  Command script_cmd;
  script_cmd.name = "SCRIPT";
  script_cmd.args.emplace_back(std::string("UNKNOWN"));

  auto result = registry_->Execute(script_cmd, context_.get());
  ASSERT_FALSE(result.success);
  EXPECT_NE(result.error.find("ERR unknown SCRIPT subcommand"),
            std::string::npos);
}

TEST_F(ScriptCommandTest, EVAL_MultipleReturnValues) {
  Command eval_cmd;
  eval_cmd.name = "EVAL";
  eval_cmd.args.emplace_back(std::string("return 1, 'two', 3"));
  eval_cmd.args.emplace_back(std::string("0"));

  auto result = registry_->Execute(eval_cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsArray());

  const auto& arr = result.response.AsArray();
  ASSERT_EQ(arr.size(), 3);
  EXPECT_TRUE(arr[0].IsInteger());
  EXPECT_EQ(arr[0].AsInteger(), 1);
  EXPECT_TRUE(arr[1].IsBulkString());
  EXPECT_EQ(arr[1].AsString(), "two");
  EXPECT_TRUE(arr[2].IsInteger());
  EXPECT_EQ(arr[2].AsInteger(), 3);
}

TEST_F(ScriptCommandTest, EVAL_ReturnNil) {
  Command eval_cmd;
  eval_cmd.name = "EVAL";
  eval_cmd.args.emplace_back(std::string("return nil"));
  eval_cmd.args.emplace_back(std::string("0"));

  auto result = registry_->Execute(eval_cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsNull());
}

TEST_F(ScriptCommandTest, EVAL_ReturnBoolean) {
  Command eval_cmd;
  eval_cmd.name = "EVAL";
  eval_cmd.args.emplace_back(std::string("return true"));
  eval_cmd.args.emplace_back(std::string("0"));

  auto result = registry_->Execute(eval_cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsInteger());
  EXPECT_EQ(result.response.AsInteger(), 1);

  // Test false
  Command eval_cmd_false;
  eval_cmd_false.name = "EVAL";
  eval_cmd_false.args.emplace_back(std::string("return false"));
  eval_cmd_false.args.emplace_back(std::string("0"));

  result = registry_->Execute(eval_cmd_false, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsInteger());
  EXPECT_EQ(result.response.AsInteger(), 0);
}

TEST_F(ScriptCommandTest, EVAL_WithNegativeNumkeys) {
  Command eval_cmd;
  eval_cmd.name = "EVAL";
  eval_cmd.args.emplace_back(std::string("return 'hello'"));
  eval_cmd.args.emplace_back(std::string("-1"));

  auto result = registry_->Execute(eval_cmd, context_.get());
  ASSERT_FALSE(result.success);
  EXPECT_NE(result.error.find("Number of keys can't be negative"),
            std::string::npos);
}

TEST_F(ScriptCommandTest, EVAL_WithTooManyKeys) {
  Command eval_cmd;
  eval_cmd.name = "EVAL";
  eval_cmd.args.emplace_back(std::string("return 'hello'"));
  eval_cmd.args.emplace_back(std::string("10"));  // 10 keys but no args

  auto result = registry_->Execute(eval_cmd, context_.get());
  ASSERT_FALSE(result.success);
  EXPECT_NE(result.error.find("Number of keys can't be greater"),
            std::string::npos);
}

}  // namespace astra::commands
