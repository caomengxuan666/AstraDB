// ==============================================================================
// Stream Command Tests
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include <gtest/gtest.h>

#include "astra/commands/command_auto_register.hpp"
#include "astra/commands/command_handler.hpp"
#include "astra/commands/database.hpp"
#include "astra/container/stream_data.hpp"
#include "astra/protocol/resp/resp_parser.hpp"

namespace astra::commands {

using astra::protocol::Command;
using astra::protocol::RespValue;

// Simple command context for testing
class TestCommandContext : public CommandContext {
 public:
  TestCommandContext() : db_(nullptr), db_index_(0), authenticated_(true) {}

  Database* GetDatabase() const override { return db_; }
  void SetDatabase(Database* db) { db_ = db; }

  int GetDBIndex() const override { return db_index_; }
  void SetDBIndex(int index) override { db_index_ = index; }

  bool IsAuthenticated() const override { return authenticated_; }
  void SetAuthenticated(bool auth) override { authenticated_ = auth; }

 private:
  Database* db_;
  int db_index_;
  bool authenticated_;
};

// Stream command tests
class StreamCommandTest : public ::testing::Test {
 protected:
  void SetUp() override {
    db_manager_ = std::make_unique<DatabaseManager>(16);
    registry_ = std::make_unique<CommandRegistry>();
    RuntimeCommandRegistry::Instance().ApplyToRegistry(*registry_);
    context_ = std::make_unique<TestCommandContext>();
    context_->SetDatabase(db_manager_->GetDatabase(0));
  }

  void TearDown() override { context_->GetDatabase()->Clear(); }

  std::unique_ptr<DatabaseManager> db_manager_;
  std::unique_ptr<CommandRegistry> registry_;
  std::unique_ptr<TestCommandContext> context_;
};

TEST_F(StreamCommandTest, XADD_XLEN) {
  // XADD mystream * field1 value1
  Command xadd_cmd;
  xadd_cmd.name = "XADD";
  xadd_cmd.args.emplace_back(std::string("mystream"));
  xadd_cmd.args.emplace_back(std::string("*"));
  xadd_cmd.args.emplace_back(std::string("field1"));
  xadd_cmd.args.emplace_back(std::string("value1"));

  auto result = registry_->Execute(xadd_cmd, context_.get());
  ASSERT_TRUE(result.success) << "XADD failed: " << result.error;
  EXPECT_TRUE(result.response.IsBulkString());

  // XLEN mystream
  Command xlen_cmd;
  xlen_cmd.name = "XLEN";
  xlen_cmd.args.emplace_back(std::string("mystream"));

  result = registry_->Execute(xlen_cmd, context_.get());
  ASSERT_TRUE(result.success) << "XLEN failed: " << result.error;
  EXPECT_TRUE(result.response.IsInteger());
  EXPECT_EQ(result.response.AsInteger(), 1);
}

TEST_F(StreamCommandTest, XADD_XREAD) {
  // XADD mystream * field1 value1
  Command xadd_cmd;
  xadd_cmd.name = "XADD";
  xadd_cmd.args.emplace_back(std::string("mystream"));
  xadd_cmd.args.emplace_back(std::string("*"));
  xadd_cmd.args.emplace_back(std::string("field1"));
  xadd_cmd.args.emplace_back(std::string("value1"));

  auto result = registry_->Execute(xadd_cmd, context_.get());
  ASSERT_TRUE(result.success);

  // XREAD COUNT 1 STREAMS mystream 0-0
  Command xread_cmd;
  xread_cmd.name = "XREAD";
  xread_cmd.args.emplace_back(std::string("COUNT"));
  xread_cmd.args.emplace_back(std::string("1"));
  xread_cmd.args.emplace_back(std::string("STREAMS"));
  xread_cmd.args.emplace_back(std::string("mystream"));
  xread_cmd.args.emplace_back(std::string("0-0"));

  result = registry_->Execute(xread_cmd, context_.get());
  ASSERT_TRUE(result.success) << "XREAD failed: " << result.error;
  EXPECT_TRUE(result.response.IsArray());
}

TEST_F(StreamCommandTest, XRANGE) {
  // XADD mystream * field1 value1
  Command xadd_cmd;
  xadd_cmd.name = "XADD";
  xadd_cmd.args.emplace_back(std::string("mystream"));
  xadd_cmd.args.emplace_back(std::string("*"));
  xadd_cmd.args.emplace_back(std::string("field1"));
  xadd_cmd.args.emplace_back(std::string("value1"));

  auto result = registry_->Execute(xadd_cmd, context_.get());
  ASSERT_TRUE(result.success);

  // XRANGE mystream - +
  Command xrange_cmd;
  xrange_cmd.name = "XRANGE";
  xrange_cmd.args.emplace_back(std::string("mystream"));
  xrange_cmd.args.emplace_back(std::string("-"));
  xrange_cmd.args.emplace_back(std::string("+"));

  result = registry_->Execute(xrange_cmd, context_.get());
  ASSERT_TRUE(result.success) << "XRANGE failed: " << result.error;
  EXPECT_TRUE(result.response.IsArray());
}

TEST_F(StreamCommandTest, XDEL) {
  // XADD mystream * field1 value1
  Command xadd_cmd;
  xadd_cmd.name = "XADD";
  xadd_cmd.args.emplace_back(std::string("mystream"));
  xadd_cmd.args.emplace_back(std::string("*"));
  xadd_cmd.args.emplace_back(std::string("field1"));
  xadd_cmd.args.emplace_back(std::string("value1"));

  auto result = registry_->Execute(xadd_cmd, context_.get());
  ASSERT_TRUE(result.success);

  // XDEL mystream <stream_id_from_xadd>
  std::string stream_id = result.response.AsString();

  Command xdel_cmd;
  xdel_cmd.name = "XDEL";
  xdel_cmd.args.emplace_back(std::string("mystream"));
  xdel_cmd.args.emplace_back(stream_id);

  result = registry_->Execute(xdel_cmd, context_.get());
  ASSERT_TRUE(result.success) << "XDEL failed: " << result.error;
  EXPECT_TRUE(result.response.IsInteger());
}

TEST_F(StreamCommandTest, XTRIM) {
  // XADD mystream * field1 value1 (multiple times)
  for (int i = 0; i < 10; ++i) {
    Command xadd_cmd;
    xadd_cmd.name = "XADD";
    xadd_cmd.args.emplace_back(std::string("mystream"));
    xadd_cmd.args.emplace_back(std::string("*"));
    xadd_cmd.args.emplace_back(std::string("field"));
    xadd_cmd.args.emplace_back(std::string("value") + std::to_string(i));

    auto result = registry_->Execute(xadd_cmd, context_.get());
    ASSERT_TRUE(result.success);
  }

  // XTRIM mystream MAXLEN 5
  Command xtrim_cmd;
  xtrim_cmd.name = "XTRIM";
  xtrim_cmd.args.emplace_back(std::string("mystream"));
  xtrim_cmd.args.emplace_back(std::string("MAXLEN"));
  xtrim_cmd.args.emplace_back(std::string("5"));

  auto result = registry_->Execute(xtrim_cmd, context_.get());
  ASSERT_TRUE(result.success) << "XTRIM failed: " << result.error;

  // XLEN mystream - should be 5
  Command xlen_cmd;
  xlen_cmd.name = "XLEN";
  xlen_cmd.args.emplace_back(std::string("mystream"));

  result = registry_->Execute(xlen_cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsInteger());
  EXPECT_EQ(result.response.AsInteger(), 5);
}

TEST_F(StreamCommandTest, XGROUP_CREATE) {
  // XADD mystream * field1 value1
  Command xadd_cmd;
  xadd_cmd.name = "XADD";
  xadd_cmd.args.emplace_back(std::string("mystream"));
  xadd_cmd.args.emplace_back(std::string("*"));
  xadd_cmd.args.emplace_back(std::string("field1"));
  xadd_cmd.args.emplace_back(std::string("value1"));

  auto result = registry_->Execute(xadd_cmd, context_.get());
  ASSERT_TRUE(result.success);

  // XGROUP CREATE mystream mygroup $
  Command xgroup_cmd;
  xgroup_cmd.name = "XGROUP";
  xgroup_cmd.args.emplace_back(std::string("CREATE"));
  xgroup_cmd.args.emplace_back(std::string("mystream"));
  xgroup_cmd.args.emplace_back(std::string("mygroup"));
  xgroup_cmd.args.emplace_back(std::string("$"));

  result = registry_->Execute(xgroup_cmd, context_.get());
  ASSERT_TRUE(result.success) << "XGROUP CREATE failed: " << result.error;
}

TEST_F(StreamCommandTest, XREADGROUP) {
  // XADD mystream * field1 value1
  Command xadd_cmd;
  xadd_cmd.name = "XADD";
  xadd_cmd.args.emplace_back(std::string("mystream"));
  xadd_cmd.args.emplace_back(std::string("*"));
  xadd_cmd.args.emplace_back(std::string("field1"));
  xadd_cmd.args.emplace_back(std::string("value1"));

  auto result = registry_->Execute(xadd_cmd, context_.get());
  ASSERT_TRUE(result.success);

  // XGROUP CREATE mystream mygroup $
  Command xgroup_cmd;
  xgroup_cmd.name = "XGROUP";
  xgroup_cmd.args.emplace_back(std::string("CREATE"));
  xgroup_cmd.args.emplace_back(std::string("mystream"));
  xgroup_cmd.args.emplace_back(std::string("mygroup"));
  xgroup_cmd.args.emplace_back(std::string("$"));

  result = registry_->Execute(xgroup_cmd, context_.get());
  ASSERT_TRUE(result.success);

  // XREADGROUP GROUP mygroup consumer COUNT 1 STREAMS mystream >
  Command xreadgroup_cmd;
  xreadgroup_cmd.name = "XREADGROUP";
  xreadgroup_cmd.args.emplace_back(std::string("GROUP"));
  xreadgroup_cmd.args.emplace_back(std::string("mygroup"));
  xreadgroup_cmd.args.emplace_back(std::string("consumer"));
  xreadgroup_cmd.args.emplace_back(std::string("COUNT"));
  xreadgroup_cmd.args.emplace_back(std::string("1"));
  xreadgroup_cmd.args.emplace_back(std::string("STREAMS"));
  xreadgroup_cmd.args.emplace_back(std::string("mystream"));
  xreadgroup_cmd.args.emplace_back(std::string(">"));

  result = registry_->Execute(xreadgroup_cmd, context_.get());
  ASSERT_TRUE(result.success) << "XREADGROUP failed: " << result.error;
}

}  // namespace astra::commands
