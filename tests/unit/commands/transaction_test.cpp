// ==============================================================================
// Transaction Command Tests
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include <gtest/gtest.h>

#include "astra/commands/command_auto_register.hpp"
#include "astra/commands/command_handler.hpp"
#include "astra/commands/database.hpp"
#include "astra/protocol/resp/resp_parser.hpp"

namespace astra::commands {

using astra::protocol::Command;
using astra::protocol::RespValue;

// Command context with transaction support
class TransactionTestContext : public CommandContext {
 public:
  TransactionTestContext()
      : db_(nullptr),
        db_index_(0),
        authenticated_(true),
        in_transaction_(false) {}

  Database* GetDatabase() const override { return db_; }
  void SetDatabase(Database* db) { db_ = db; }

  int GetDBIndex() const override { return db_index_; }
  void SetDBIndex(int index) override { db_index_ = index; }

  bool IsAuthenticated() const override { return authenticated_; }
  void SetAuthenticated(bool auth) override { authenticated_ = auth; }

  bool IsInTransaction() const override { return in_transaction_; }
  void BeginTransaction() override { in_transaction_ = true; }
  void QueueCommand(const protocol::Command& cmd) override {
    queued_commands_.push_back(cmd);
  }
  absl::InlinedVector<protocol::Command, 16> GetQueuedCommands()
      const override {
    return queued_commands_;
  }
  void ClearQueuedCommands() override { queued_commands_.clear(); }
  void DiscardTransaction() override {
    in_transaction_ = false;
    queued_commands_.clear();
    watched_keys_.clear();
    watched_key_versions_.clear();
  }
  void WatchKey(const std::string& key, uint64_t version) override {
    watched_keys_.insert(key);
    watched_key_versions_[key] = version;
  }
  const absl::flat_hash_set<std::string>& GetWatchedKeys() const override {
    return watched_keys_;
  }
  bool IsWatchedKeyModified(
      const absl::AnyInvocable<uint64_t(const std::string&) const>& get_version)
      const override {
    for (const auto& key : watched_keys_) {
      auto it = watched_key_versions_.find(key);
      if (it != watched_key_versions_.end()) {
        uint64_t current_version = get_version(key);
        if (current_version != it->second) {
          return true;
        }
      }
    }
    return false;
  }
  void ClearWatchedKeys() override {
    watched_keys_.clear();
    watched_key_versions_.clear();
  }

  // Helper to simulate server.cpp transaction handling
  CommandResult ExecuteWithTransactionHandling(const Command& cmd) {
    // Check if in transaction mode
    bool in_transaction = IsInTransaction();

    // Commands that are allowed inside MULTI
    static const absl::flat_hash_set<std::string> kTransactionCommands = {
        "MULTI", "EXEC", "DISCARD", "WATCH", "UNWATCH"};

    // Handle EXEC specially - execute all queued commands
    if (cmd.name == "EXEC") {
      if (!in_transaction) {
        return CommandResult(false, "ERR EXEC without MULTI");
      }

      // Get all queued commands
      auto queued_commands = GetQueuedCommands();
      std::vector<RespValue> results;
      results.reserve(queued_commands.size());

      // Execute each queued command
      for (const auto& queued_cmd : queued_commands) {
        auto cmd_result = registry_->Execute(queued_cmd, this);
        results.push_back(cmd_result.response);
      }

      // Clear transaction state
      ClearQueuedCommands();
      ClearWatchedKeys();
      DiscardTransaction();

      // Return array of results
      RespValue response;
      response.SetArray(std::move(results));
      return CommandResult(response);
    }

    // If in transaction and command is not a transaction control command, queue
    // it
    if (in_transaction &&
        kTransactionCommands.find(cmd.name) == kTransactionCommands.end()) {
      QueueCommand(cmd);

      // Send QUEUED response
      RespValue queued_resp;
      queued_resp.SetString("QUEUED", RespType::kSimpleString);
      return CommandResult(queued_resp);
    }

    // Execute normally
    return registry_->Execute(cmd, this);
  }

  // Set registry for transaction handling
  void SetRegistry(CommandRegistry* registry) { registry_ = registry; }

 private:
  Database* db_;
  int db_index_;
  bool authenticated_;
  bool in_transaction_;
  absl::InlinedVector<protocol::Command, 16> queued_commands_;
  absl::flat_hash_set<std::string> watched_keys_;
  absl::flat_hash_map<std::string, uint64_t> watched_key_versions_;
  CommandRegistry* registry_ = nullptr;
};

// Transaction command tests
class TransactionCommandTest : public ::testing::Test {
 protected:
  void SetUp() override {
    db_manager_ = std::make_unique<DatabaseManager>(16);
    registry_ = std::make_unique<CommandRegistry>();
    RuntimeCommandRegistry::Instance().ApplyToRegistry(*registry_);
    context_ = std::make_unique<TransactionTestContext>();
    context_->SetDatabase(db_manager_->GetDatabase(0));
    context_->SetRegistry(registry_.get());
  }

  void TearDown() override { context_->GetDatabase()->Clear(); }

  std::unique_ptr<DatabaseManager> db_manager_;
  std::unique_ptr<CommandRegistry> registry_;
  std::unique_ptr<TransactionTestContext> context_;
};

TEST_F(TransactionCommandTest, MULTI_EXEC) {
  // SET key1 value1
  Command set_cmd1;
  set_cmd1.name = "SET";
  set_cmd1.args.emplace_back(std::string("key1"));
  set_cmd1.args.emplace_back(std::string("value1"));

  // SET key2 value2
  Command set_cmd2;
  set_cmd2.name = "SET";
  set_cmd2.args.emplace_back(std::string("key2"));
  set_cmd2.args.emplace_back(std::string("value2"));

  // MULTI
  Command multi_cmd;
  multi_cmd.name = "MULTI";

  auto result = context_->ExecuteWithTransactionHandling(multi_cmd);
  ASSERT_TRUE(result.success) << "MULTI failed: " << result.error;
  EXPECT_TRUE(result.response.IsSimpleString());
  EXPECT_EQ(result.response.AsString(), "OK");
  EXPECT_TRUE(context_->IsInTransaction());

  // SET key1 value1 (should be queued)
  result = context_->ExecuteWithTransactionHandling(set_cmd1);
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsSimpleString());
  EXPECT_EQ(result.response.AsString(), "QUEUED");

  // SET key2 value2 (should be queued)
  result = context_->ExecuteWithTransactionHandling(set_cmd2);
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsSimpleString());
  EXPECT_EQ(result.response.AsString(), "QUEUED");

  // EXEC
  Command exec_cmd;
  exec_cmd.name = "EXEC";

  result = context_->ExecuteWithTransactionHandling(exec_cmd);
  ASSERT_TRUE(result.success) << "EXEC failed: " << result.error;
  EXPECT_TRUE(result.response.IsArray());

  // Verify both commands were executed
  EXPECT_FALSE(context_->IsInTransaction());

  // Verify keys were actually set
  Command get_cmd1;
  get_cmd1.name = "GET";
  get_cmd1.args.emplace_back(std::string("key1"));

  result = registry_->Execute(get_cmd1, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsBulkString());
  EXPECT_EQ(result.response.AsString(), "value1");

  Command get_cmd2;
  get_cmd2.name = "GET";
  get_cmd2.args.emplace_back(std::string("key2"));

  result = registry_->Execute(get_cmd2, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsBulkString());
  EXPECT_EQ(result.response.AsString(), "value2");
}

TEST_F(TransactionCommandTest, DISCARD) {
  // MULTI
  Command multi_cmd;
  multi_cmd.name = "MULTI";

  auto result = context_->ExecuteWithTransactionHandling(multi_cmd);
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsSimpleString());
  EXPECT_EQ(result.response.AsString(), "OK");
  EXPECT_TRUE(context_->IsInTransaction());

  // SET key value (queued)
  Command set_cmd;
  set_cmd.name = "SET";
  set_cmd.args.emplace_back(std::string("key"));
  set_cmd.args.emplace_back(std::string("value"));

  result = context_->ExecuteWithTransactionHandling(set_cmd);
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsSimpleString());
  EXPECT_EQ(result.response.AsString(), "QUEUED");

  // DISCARD
  Command discard_cmd;
  discard_cmd.name = "DISCARD";

  result = context_->ExecuteWithTransactionHandling(discard_cmd);
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsSimpleString());
  EXPECT_EQ(result.response.AsString(), "OK");
  EXPECT_FALSE(context_->IsInTransaction());

  // Verify key was not set
  Command get_cmd;
  get_cmd.name = "GET";
  get_cmd.args.emplace_back(std::string("key"));

  result = registry_->Execute(get_cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsNull());
}

TEST_F(TransactionCommandTest, EXEC_Without_MULTI) {
  // EXEC without MULTI should fail
  Command exec_cmd;
  exec_cmd.name = "EXEC";

  auto result = context_->ExecuteWithTransactionHandling(exec_cmd);
  ASSERT_FALSE(result.success);
  EXPECT_NE(result.error.find("EXEC without MULTI"), std::string::npos);
}

TEST_F(TransactionCommandTest, WATCH_XEXEC) {
  // SET key1 value1
  Command set_cmd;
  set_cmd.name = "SET";
  set_cmd.args.emplace_back(std::string("key1"));
  set_cmd.args.emplace_back(std::string("value1"));

  auto result = registry_->Execute(set_cmd, context_.get());
  ASSERT_TRUE(result.success);

  // WATCH key1
  Command watch_cmd;
  watch_cmd.name = "WATCH";
  watch_cmd.args.emplace_back(std::string("key1"));

  result = registry_->Execute(watch_cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsSimpleString());
  EXPECT_EQ(result.response.AsString(), "OK");

  // MULTI
  Command multi_cmd;
  multi_cmd.name = "MULTI";

  result = context_->ExecuteWithTransactionHandling(multi_cmd);
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsSimpleString());
  EXPECT_EQ(result.response.AsString(), "OK");

  // SET key1 newvalue (queued)
  Command set_cmd2;
  set_cmd2.name = "SET";
  set_cmd2.args.emplace_back(std::string("key1"));
  set_cmd2.args.emplace_back(std::string("newvalue"));

  result = context_->ExecuteWithTransactionHandling(set_cmd2);
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsSimpleString());
  EXPECT_EQ(result.response.AsString(), "QUEUED");

  // Note: In the actual server, WATCH would check if the key was modified
  // In our test context, the version check is simplified
  // EXEC should return the result of the queued command
  Command exec_cmd;
  exec_cmd.name = "EXEC";

  result = context_->ExecuteWithTransactionHandling(exec_cmd);
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsArray());

  // Verify the key was updated
  Command get_cmd;
  get_cmd.name = "GET";
  get_cmd.args.emplace_back(std::string("key1"));

  result = registry_->Execute(get_cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsBulkString());
  EXPECT_EQ(result.response.AsString(), "newvalue");
}

TEST_F(TransactionCommandTest, UNWATCH) {
  // WATCH key1
  Command watch_cmd;
  watch_cmd.name = "WATCH";
  watch_cmd.args.emplace_back(std::string("key1"));

  auto result = registry_->Execute(watch_cmd, context_.get());
  ASSERT_TRUE(result.success);

  // UNWATCH
  Command unwatch_cmd;
  unwatch_cmd.name = "UNWATCH";

  result = registry_->Execute(unwatch_cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsSimpleString());
  EXPECT_EQ(result.response.AsString(), "OK");
}

}  // namespace astra::commands
