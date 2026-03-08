// ==============================================================================
// Command Tests
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include <gtest/gtest.h>
#include "astra/commands/command_handler.hpp"
#include "astra/commands/database.hpp"
#include "astra/commands/command_auto_register.hpp"
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

// Command handler tests
class CommandHandlerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    db_manager_ = std::make_unique<DatabaseManager>(16);
    // Database manager will be passed through context

    registry_ = std::make_unique<CommandRegistry>();
    // Auto-register all commands via RuntimeCommandRegistry
    RuntimeCommandRegistry::Instance().ApplyToRegistry(*registry_);

    context_ = std::make_unique<TestCommandContext>();
    context_->SetDatabase(db_manager_->GetDatabase(0));
  }

  void TearDown() override {
    // Cleanup handled by smart pointers
  }

  std::unique_ptr<DatabaseManager> db_manager_;
  std::unique_ptr<CommandRegistry> registry_;
  std::unique_ptr<TestCommandContext> context_;
};

TEST_F(CommandHandlerTest, GetSet) {
  // SET key value
  Command set_cmd;
  set_cmd.name = "SET";
  set_cmd.args.emplace_back(std::string("key1"));
  set_cmd.args.emplace_back(std::string("value1"));

  auto result = registry_->Execute(set_cmd, context_.get());
  if (!result.success) {
    std::cerr << "SET failed: " << result.error << std::endl;
  }
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsSimpleString());

  // GET key1
  Command get_cmd;
  get_cmd.name = "GET";
  get_cmd.args.emplace_back(std::string("key1"));

  result = registry_->Execute(get_cmd, context_.get());
  if (!result.success) {
    std::cerr << "GET failed: " << result.error << std::endl;
  }
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsBulkString());
  EXPECT_EQ(result.response.AsString(), "value1");
}

TEST_F(CommandHandlerTest, GetNonExistentKey) {
  Command cmd;
  cmd.name = "GET";
  cmd.args.emplace_back(std::string("nonexistent"));

  auto result = registry_->Execute(cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsNull());
}

TEST_F(CommandHandlerTest, SetWithNX) {
  // SET key value NX
  Command cmd;
  cmd.name = "SET";
  cmd.args.emplace_back(std::string("key1"));
  cmd.args.emplace_back(std::string("value1"));
  cmd.args.emplace_back(std::string("NX"));

  auto result = registry_->Execute(cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsSimpleString());

  // SET key value2 NX (should fail)
  cmd.args[1] = RespValue(std::string("value2"));
  result = registry_->Execute(cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsNull());

  // Verify value is still value1
  Command get_cmd;
  get_cmd.name = "GET";
  get_cmd.args.emplace_back(std::string("key1"));
  result = registry_->Execute(get_cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.response.AsString(), "value1");
}

TEST_F(CommandHandlerTest, SetWithXX) {
  // SET key value XX (should fail)
  Command cmd;
  cmd.name = "SET";
  cmd.args.emplace_back(std::string("key1"));
  cmd.args.emplace_back(std::string("value1"));
  cmd.args.emplace_back(std::string("XX"));

  auto result = registry_->Execute(cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsNull());

  // First set the key
  cmd.args.pop_back();
  result = registry_->Execute(cmd, context_.get());
  ASSERT_TRUE(result.success);

  // SET key value2 XX (should succeed)
  cmd.args[1] = RespValue(std::string("value2"));
  cmd.args.emplace_back(std::string("XX"));
  result = registry_->Execute(cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsSimpleString());

  // Verify value is value2
  Command get_cmd;
  get_cmd.name = "GET";
  get_cmd.args.emplace_back(std::string("key1"));
  result = registry_->Execute(get_cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.response.AsString(), "value2");
}

TEST_F(CommandHandlerTest, SetWithEX) {
  // SET key value EX 1
  Command cmd;
  cmd.name = "SET";
  cmd.args.emplace_back(std::string("key1"));
  cmd.args.emplace_back(std::string("value1"));
  cmd.args.emplace_back(std::string("EX"));
  cmd.args.emplace_back(std::string("1"));

  auto result = registry_->Execute(cmd, context_.get());
  ASSERT_TRUE(result.success);

  // Sleep for 1.1 seconds to let it expire
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));

  // GET key1 (should return null)
  Command get_cmd;
  get_cmd.name = "GET";
  get_cmd.args.emplace_back(std::string("key1"));
  result = registry_->Execute(get_cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsNull());
}

TEST_F(CommandHandlerTest, Del) {
  // SET multiple keys
  for (int i = 0; i < 5; ++i) {
    Command set_cmd;
    set_cmd.name = "SET";
    set_cmd.args.emplace_back(std::string("key") + std::to_string(i));
    set_cmd.args.emplace_back(std::string("value") + std::to_string(i));
    registry_->Execute(set_cmd, context_.get());
  }

  // DEL 3 keys
  Command del_cmd;
  del_cmd.name = "DEL";
  del_cmd.args.emplace_back(std::string("key0"));
  del_cmd.args.emplace_back(std::string("key1"));
  del_cmd.args.emplace_back(std::string("key2"));

  auto result = registry_->Execute(del_cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsInteger());
  EXPECT_EQ(result.response.AsInteger(), 3);
}

TEST_F(CommandHandlerTest, MGet) {
  // SET multiple keys
  for (int i = 0; i < 5; ++i) {
    Command set_cmd;
    set_cmd.name = "SET";
    set_cmd.args.emplace_back(std::string("key") + std::to_string(i));
    set_cmd.args.emplace_back(std::string("value") + std::to_string(i));
    registry_->Execute(set_cmd, context_.get());
  }

  // MGET keys
  Command cmd;
  cmd.name = "MGET";
  cmd.args.emplace_back(std::string("key0"));
  cmd.args.emplace_back(std::string("key1"));
  cmd.args.emplace_back(std::string("key99"));  // Non-existent
  cmd.args.emplace_back(std::string("key2"));

  auto result = registry_->Execute(cmd, context_.get());
  ASSERT_TRUE(result.success);
  ASSERT_TRUE(result.response.IsArray());
  ASSERT_EQ(result.response.ArraySize(), 4);

  const auto& array = result.response.AsArray();
  EXPECT_EQ(array[0].AsString(), "value0");
  EXPECT_EQ(array[1].AsString(), "value1");
  EXPECT_TRUE(array[2].IsNull());
  EXPECT_EQ(array[3].AsString(), "value2");
}

TEST_F(CommandHandlerTest, MSet) {
  // MSET multiple key-value pairs
  Command cmd;
  cmd.name = "MSET";
  cmd.args.emplace_back(std::string("key1"));
  cmd.args.emplace_back(std::string("value1"));
  cmd.args.emplace_back(std::string("key2"));
  cmd.args.emplace_back(std::string("value2"));
  cmd.args.emplace_back(std::string("key3"));
  cmd.args.emplace_back(std::string("value3"));

  auto result = registry_->Execute(cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsSimpleString());

  // Verify values
  for (int i = 1; i <= 3; ++i) {
    Command get_cmd;
    get_cmd.name = "GET";
    get_cmd.args.emplace_back(std::string("key") + std::to_string(i));
    result = registry_->Execute(get_cmd, context_.get());
    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.response.AsString(), "value" + std::to_string(i));
  }
}

TEST_F(CommandHandlerTest, Exists) {
  // SET two keys
  Command set_cmd;
  set_cmd.name = "SET";
  set_cmd.args.emplace_back(std::string("key1"));
  set_cmd.args.emplace_back(std::string("value1"));
  registry_->Execute(set_cmd, context_.get());

  set_cmd.args[0] = RespValue(std::string("key2"));
  registry_->Execute(set_cmd, context_.get());

  // EXISTS key1 key2 key99
  Command cmd;
  cmd.name = "EXISTS";
  cmd.args.emplace_back(std::string("key1"));
  cmd.args.emplace_back(std::string("key2"));
  cmd.args.emplace_back(std::string("key99"));

  auto result = registry_->Execute(cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsInteger());
  EXPECT_EQ(result.response.AsInteger(), 2);
}

TEST_F(CommandHandlerTest, UnknownCommand) {
  Command cmd;
  cmd.name = "UNKNOWN";
  cmd.args.emplace_back(std::string("arg1"));

  auto result = registry_->Execute(cmd, context_.get());
  ASSERT_FALSE(result.success);
  EXPECT_FALSE(result.error.empty());
}

TEST_F(CommandHandlerTest, WrongArgumentCount) {
  // GET without arguments
  Command cmd;
  cmd.name = "GET";

  auto result = registry_->Execute(cmd, context_.get());
  ASSERT_FALSE(result.success);
  EXPECT_NE(result.error.find("wrong number of arguments"), std::string::npos);
}

// Hash commands tests
TEST_F(CommandHandlerTest, HSetHGet) {
  // HSET key field value
  Command cmd;
  cmd.name = "HSET";
  cmd.args.emplace_back(std::string("hash1"));
  cmd.args.emplace_back(std::string("field1"));
  cmd.args.emplace_back(std::string("value1"));

  auto result = registry_->Execute(cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsInteger());
  EXPECT_EQ(result.response.AsInteger(), 1);  // New field

  // HGET hash1 field1
  Command get_cmd;
  get_cmd.name = "HGET";
  get_cmd.args.emplace_back(std::string("hash1"));
  get_cmd.args.emplace_back(std::string("field1"));

  result = registry_->Execute(get_cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.response.AsString(), "value1");
}

TEST_F(CommandHandlerTest, HSetUpdate) {
  // HSET key field value
  Command cmd;
  cmd.name = "HSET";
  cmd.args.emplace_back(std::string("hash1"));
  cmd.args.emplace_back(std::string("field1"));
  cmd.args.emplace_back(std::string("value1"));

  registry_->Execute(cmd, context_.get());

  // HSET key field value2 (update)
  cmd.args[2] = RespValue(std::string("value2"));
  auto result = registry_->Execute(cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.response.AsInteger(), 0);  // Updated

  // Verify updated value
  Command get_cmd;
  get_cmd.name = "HGET";
  get_cmd.args.emplace_back(std::string("hash1"));
  get_cmd.args.emplace_back(std::string("field1"));

  result = registry_->Execute(get_cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.response.AsString(), "value2");
}

TEST_F(CommandHandlerTest, HDel) {
  // HSET multiple fields
  for (int i = 0; i < 3; ++i) {
    Command cmd;
    cmd.name = "HSET";
    cmd.args.emplace_back(std::string("hash1"));
    cmd.args.emplace_back(std::string("field") + std::to_string(i));
    cmd.args.emplace_back(std::string("value") + std::to_string(i));
    registry_->Execute(cmd, context_.get());
  }

  // HDEL one field
  Command cmd;
  cmd.name = "HDEL";
  cmd.args.emplace_back(std::string("hash1"));
  cmd.args.emplace_back(std::string("field0"));

  auto result = registry_->Execute(cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.response.AsInteger(), 1);
}

TEST_F(CommandHandlerTest, HExists) {
  // HSET key field value
  Command cmd;
  cmd.name = "HSET";
  cmd.args.emplace_back(std::string("hash1"));
  cmd.args.emplace_back(std::string("field1"));
  cmd.args.emplace_back(std::string("value1"));
  registry_->Execute(cmd, context_.get());

  // HEXISTS hash1 field1
  Command exists_cmd;
  exists_cmd.name = "HEXISTS";
  exists_cmd.args.emplace_back(std::string("hash1"));
  exists_cmd.args.emplace_back(std::string("field1"));

  auto result = registry_->Execute(exists_cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.response.AsInteger(), 1);

  // HEXISTS hash1 field99
  exists_cmd.args[1] = RespValue(std::string("field99"));
  result = registry_->Execute(exists_cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.response.AsInteger(), 0);
}

TEST_F(CommandHandlerTest, HLen) {
  // HSET multiple fields
  for (int i = 0; i < 5; ++i) {
    Command cmd;
    cmd.name = "HSET";
    cmd.args.emplace_back(std::string("hash1"));
    cmd.args.emplace_back(std::string("field") + std::to_string(i));
    cmd.args.emplace_back(std::string("value") + std::to_string(i));
    registry_->Execute(cmd, context_.get());
  }

  // HLEN hash1
  Command cmd;
  cmd.name = "HLEN";
  cmd.args.emplace_back(std::string("hash1"));

  auto result = registry_->Execute(cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.response.AsInteger(), 5);
}

// Set commands tests
TEST_F(CommandHandlerTest, SAddSRem) {
  // SADD key member1 member2 member3
  Command cmd;
  cmd.name = "SADD";
  cmd.args.emplace_back(std::string("set1"));
  cmd.args.emplace_back(std::string("member1"));
  cmd.args.emplace_back(std::string("member2"));
  cmd.args.emplace_back(std::string("member3"));

  auto result = registry_->Execute(cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.response.AsInteger(), 3);

  // SREM set1 member2
  Command rem_cmd;
  rem_cmd.name = "SREM";
  rem_cmd.args.emplace_back(std::string("set1"));
  rem_cmd.args.emplace_back(std::string("member2"));

  result = registry_->Execute(rem_cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.response.AsInteger(), 1);
}

TEST_F(CommandHandlerTest, SIsMember) {
  // SADD key member1
  Command cmd;
  cmd.name = "SADD";
  cmd.args.emplace_back(std::string("set1"));
  cmd.args.emplace_back(std::string("member1"));
  registry_->Execute(cmd, context_.get());

  // SISMEMBER set1 member1
  Command ismember_cmd;
  ismember_cmd.name = "SISMEMBER";
  ismember_cmd.args.emplace_back(std::string("set1"));
  ismember_cmd.args.emplace_back(std::string("member1"));

  auto result = registry_->Execute(ismember_cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.response.AsInteger(), 1);

  // SISMEMBER set1 member99
  ismember_cmd.args[1] = RespValue(std::string("member99"));
  result = registry_->Execute(ismember_cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.response.AsInteger(), 0);
}

TEST_F(CommandHandlerTest, SCard) {
  // SADD multiple members
  for (int i = 0; i < 5; ++i) {
    Command cmd;
    cmd.name = "SADD";
    cmd.args.emplace_back(std::string("set1"));
    cmd.args.emplace_back(std::string("member") + std::to_string(i));
    registry_->Execute(cmd, context_.get());
  }

  // SCARD set1
  Command cmd;
  cmd.name = "SCARD";
  cmd.args.emplace_back(std::string("set1"));

  auto result = registry_->Execute(cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.response.AsInteger(), 5);
}

// ZSet commands tests
TEST_F(CommandHandlerTest, ZAddZRange) {
  // ZADD key score member
  Command cmd;
  cmd.name = "ZADD";
  cmd.args.emplace_back(std::string("zset1"));
  cmd.args.emplace_back(std::string("10.5"));
  cmd.args.emplace_back(std::string("member1"));

  auto result = registry_->Execute(cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.response.AsInteger(), 1);

  // Add more members
  cmd.args[1] = RespValue(std::string("5.0"));
  cmd.args[2] = RespValue(std::string("member2"));
  registry_->Execute(cmd, context_.get());

  cmd.args[1] = RespValue(std::string("15.0"));
  cmd.args[2] = RespValue(std::string("member3"));
  registry_->Execute(cmd, context_.get());

  // ZRANGE zset1 0 -1
  Command range_cmd;
  range_cmd.name = "ZRANGE";
  range_cmd.args.emplace_back(std::string("zset1"));
  range_cmd.args.emplace_back(std::string("0"));
  range_cmd.args.emplace_back(std::string("-1"));

  result = registry_->Execute(range_cmd, context_.get());
  ASSERT_TRUE(result.success);
  ASSERT_TRUE(result.response.IsArray());
  ASSERT_EQ(result.response.ArraySize(), 3);

  const auto& array = result.response.AsArray();
  EXPECT_EQ(array[0].AsString(), "member2");  // Score 5.0
  EXPECT_EQ(array[1].AsString(), "member1");  // Score 10.5
  EXPECT_EQ(array[2].AsString(), "member3");  // Score 15.0
}

TEST_F(CommandHandlerTest, ZRem) {
  // ZADD key score member
  Command cmd;
  cmd.name = "ZADD";
  cmd.args.emplace_back(std::string("zset1"));
  cmd.args.emplace_back(std::string("10.0"));
  cmd.args.emplace_back(std::string("member1"));
  registry_->Execute(cmd, context_.get());

  cmd.args[1] = RespValue(std::string("20.0"));
  cmd.args[2] = RespValue(std::string("member2"));
  registry_->Execute(cmd, context_.get());

  // ZREM zset1 member1
  Command rem_cmd;
  rem_cmd.name = "ZREM";
  rem_cmd.args.emplace_back(std::string("zset1"));
  rem_cmd.args.emplace_back(std::string("member1"));

  auto result = registry_->Execute(rem_cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.response.AsInteger(), 1);

  // ZCARD should return 1
  Command card_cmd;
  card_cmd.name = "ZCARD";
  card_cmd.args.emplace_back(std::string("zset1"));

  result = registry_->Execute(card_cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.response.AsInteger(), 1);
}

TEST_F(CommandHandlerTest, ZScore) {
  // ZADD key score member
  Command cmd;
  cmd.name = "ZADD";
  cmd.args.emplace_back(std::string("zset1"));
  cmd.args.emplace_back(std::string("10.5"));
  cmd.args.emplace_back(std::string("member1"));

  registry_->Execute(cmd, context_.get());

  // ZSCORE zset1 member1
  Command score_cmd;
  score_cmd.name = "ZSCORE";
  score_cmd.args.emplace_back(std::string("zset1"));
  score_cmd.args.emplace_back(std::string("member1"));

  auto result = registry_->Execute(score_cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_DOUBLE_EQ(result.response.AsDouble(), 10.5);

  // ZSCORE zset1 member99
  score_cmd.args[1] = RespValue(std::string("member99"));
  result = registry_->Execute(score_cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsNull());
}

TEST_F(CommandHandlerTest, ZCard) {
  // ZADD multiple members
  for (int i = 0; i < 5; ++i) {
    Command cmd;
    cmd.name = "ZADD";
    cmd.args.emplace_back(std::string("zset1"));
    cmd.args.emplace_back(std::to_string(i * 10.0));
    cmd.args.emplace_back(std::string("member") + std::to_string(i));
    registry_->Execute(cmd, context_.get());
  }

  // ZCARD zset1
  Command cmd;
  cmd.name = "ZCARD";
  cmd.args.emplace_back(std::string("zset1"));

  auto result = registry_->Execute(cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.response.AsInteger(), 5);
}

TEST_F(CommandHandlerTest, ZCount) {
  // ZADD multiple members with different scores
  Command cmd;
  cmd.name = "ZADD";
  cmd.args.emplace_back(std::string("zset1"));
  cmd.args.emplace_back(std::string("10.0"));
  cmd.args.emplace_back(std::string("member1"));
  registry_->Execute(cmd, context_.get());

  cmd.args[1] = RespValue(std::string("20.0"));
  cmd.args[2] = RespValue(std::string("member2"));
  registry_->Execute(cmd, context_.get());

  cmd.args[1] = RespValue(std::string("30.0"));
  cmd.args[2] = RespValue(std::string("member3"));
  registry_->Execute(cmd, context_.get());

  cmd.args[1] = RespValue(std::string("40.0"));
  cmd.args[2] = RespValue(std::string("member4"));
  registry_->Execute(cmd, context_.get());

  // ZCOUNT zset1 15 35
  Command count_cmd;
  count_cmd.name = "ZCOUNT";
  count_cmd.args.emplace_back(std::string("zset1"));
  count_cmd.args.emplace_back(std::string("15.0"));
  count_cmd.args.emplace_back(std::string("35.0"));

  auto result = registry_->Execute(count_cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.response.AsInteger(), 2);  // member2 (20.0) and member3 (30.0)
}

TEST_F(CommandHandlerTest, ZRangeWithScores) {
  // ZADD key score member
  Command cmd;
  cmd.name = "ZADD";
  cmd.args.emplace_back(std::string("zset1"));
  cmd.args.emplace_back(std::string("10.0"));
  cmd.args.emplace_back(std::string("member1"));
  registry_->Execute(cmd, context_.get());

  cmd.args[1] = RespValue(std::string("20.0"));
  cmd.args[2] = RespValue(std::string("member2"));
  registry_->Execute(cmd, context_.get());

  // ZRANGE zset1 0 -1 WITHSCORES
  Command range_cmd;
  range_cmd.name = "ZRANGE";
  range_cmd.args.emplace_back(std::string("zset1"));
  range_cmd.args.emplace_back(std::string("0"));
  range_cmd.args.emplace_back(std::string("-1"));
  range_cmd.args.emplace_back(std::string("WITHSCORES"));

  auto result = registry_->Execute(range_cmd, context_.get());
  ASSERT_TRUE(result.success);
  ASSERT_TRUE(result.response.IsArray());
  ASSERT_EQ(result.response.ArraySize(), 4);  // 2 members * 2 (member + score)

  const auto& array = result.response.AsArray();
  EXPECT_EQ(array[0].AsString(), "member1");
  EXPECT_DOUBLE_EQ(array[1].AsDouble(), 10.0);
  EXPECT_EQ(array[2].AsString(), "member2");
  EXPECT_DOUBLE_EQ(array[3].AsDouble(), 20.0);
}

// COMMAND command tests
TEST_F(CommandHandlerTest, CommandInfoGet) {
  // COMMAND INFO GET
  Command cmd;
  cmd.name = "COMMAND";
  cmd.args.emplace_back(std::string("INFO"));
  cmd.args.emplace_back(std::string("GET"));

  auto result = registry_->Execute(cmd, context_.get());
  ASSERT_TRUE(result.success);
  ASSERT_TRUE(result.response.IsArray());
  
  const auto& cmd_info = result.response.AsArray();
  ASSERT_FALSE(cmd_info.empty());
  
  // Verify COMMAND INFO GET returns 10 elements
  const auto& get_info = cmd_info[0].AsArray();
  ASSERT_EQ(get_info.size(), 10);
  
  // 1. name
  EXPECT_EQ(get_info[0].AsString(), "get");
  
  // 2. arity
  EXPECT_EQ(get_info[1].AsInteger(), 2);
  
  // 3. flags (array)
  ASSERT_TRUE(get_info[2].IsArray());
  const auto& flags = get_info[2].AsArray();
  ASSERT_GE(flags.size(), 1);
  EXPECT_EQ(flags[0].AsString(), "readonly");
  
  // 4-6. first_key, last_key, step
  EXPECT_EQ(get_info[3].AsInteger(), 1);
  EXPECT_EQ(get_info[4].AsInteger(), 1);
  EXPECT_EQ(get_info[5].AsInteger(), 1);
  
  // 7. categories (array)
  ASSERT_TRUE(get_info[6].IsArray());
  const auto& categories = get_info[6].AsArray();
  ASSERT_GE(categories.size(), 1);
  EXPECT_TRUE(categories[0].AsString() == "@read" || categories[0].AsString() == "@string");
  
  // 8. tips (array)
  ASSERT_TRUE(get_info[7].IsArray());
  
  // 9. key specs (array)
  ASSERT_TRUE(get_info[8].IsArray());
  
  // 10. subcommands (array)
  ASSERT_TRUE(get_info[9].IsArray());
}

TEST_F(CommandHandlerTest, CommandInfoSet) {
  // COMMAND INFO SET
  Command cmd;
  cmd.name = "COMMAND";
  cmd.args.emplace_back(std::string("INFO"));
  cmd.args.emplace_back(std::string("SET"));

  auto result = registry_->Execute(cmd, context_.get());
  ASSERT_TRUE(result.success);
  ASSERT_TRUE(result.response.IsArray());
  
  const auto& cmd_info = result.response.AsArray();
  ASSERT_FALSE(cmd_info.empty());
  
  const auto& set_info = cmd_info[0].AsArray();
  ASSERT_EQ(set_info.size(), 10);
  
  // Verify SET command info
  EXPECT_EQ(set_info[0].AsString(), "set");
  EXPECT_EQ(set_info[1].AsInteger(), -3);  // SET key value [options]
  
  // flags should include "write"
  const auto& flags = set_info[2].AsArray();
  bool has_write = false;
  for (const auto& flag : flags) {
    if (flag.AsString() == "write") {
      has_write = true;
      break;
    }
  }
  EXPECT_TRUE(has_write);
  
  // categories should include @write
  const auto& categories = set_info[6].AsArray();
  bool has_write_cat = false;
  for (const auto& cat : categories) {
    if (cat.AsString() == "@write") {
      has_write_cat = true;
      break;
    }
  }
  EXPECT_TRUE(has_write_cat);
}

TEST_F(CommandHandlerTest, CommandInfoScan) {
  // COMMAND INFO SCAN
  Command cmd;
  cmd.name = "COMMAND";
  cmd.args.emplace_back(std::string("INFO"));
  cmd.args.emplace_back(std::string("SCAN"));

  auto result = registry_->Execute(cmd, context_.get());
  ASSERT_TRUE(result.success);
  ASSERT_TRUE(result.response.IsArray());
  
  const auto& cmd_info = result.response.AsArray();
  ASSERT_FALSE(cmd_info.empty());
  
  const auto& scan_info = cmd_info[0].AsArray();
  ASSERT_EQ(scan_info.size(), 10);
  
  // Verify SCAN command info
  EXPECT_EQ(scan_info[0].AsString(), "scan");
  EXPECT_EQ(scan_info[1].AsInteger(), -2);  // SCAN cursor [MATCH pattern] [COUNT count]
  
  // tips should include nondeterministic_output
  const auto& tips = scan_info[7].AsArray();
  bool has_nondet = false;
  for (const auto& tip : tips) {
    if (tip.AsString() == "nondeterministic_output") {
      has_nondet = true;
      break;
    }
  }
  EXPECT_TRUE(has_nondet);
}

TEST_F(CommandHandlerTest, CommandList) {
  // COMMAND LIST
  Command cmd;
  cmd.name = "COMMAND";
  cmd.args.emplace_back(std::string("LIST"));

  auto result = registry_->Execute(cmd, context_.get());
  ASSERT_TRUE(result.success);
  ASSERT_TRUE(result.response.IsArray());
  
  const auto& cmd_list = result.response.AsArray();
  ASSERT_GT(cmd_list.size(), 0);  // Should have at least some commands
  
  // Verify all elements are strings
  for (const auto& cmd_name : cmd_list) {
    EXPECT_TRUE(cmd_name.IsBulkString() || cmd_name.IsSimpleString());
  }
}

TEST_F(CommandHandlerTest, CommandCount) {
  // COMMAND COUNT
  Command cmd;
  cmd.name = "COMMAND";
  cmd.args.emplace_back(std::string("COUNT"));

  auto result = registry_->Execute(cmd, context_.get());
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsInteger());
  EXPECT_GT(result.response.AsInteger(), 0);  // Should have at least some commands
}

TEST_F(CommandHandlerTest, CommandNoArgs) {
  // COMMAND (no arguments - return all commands)
  Command cmd;
  cmd.name = "COMMAND";

  auto result = registry_->Execute(cmd, context_.get());
  ASSERT_TRUE(result.success);
  ASSERT_TRUE(result.response.IsArray());
  
  const auto& all_commands = result.response.AsArray();
  ASSERT_GT(all_commands.size(), 0);
  
  // Each command should be an array with 10 elements
  for (const auto& cmd_info : all_commands) {
    ASSERT_TRUE(cmd_info.IsArray());
    const auto& info = cmd_info.AsArray();
    ASSERT_EQ(info.size(), 10);
    
    // Verify structure
    EXPECT_TRUE(info[0].IsBulkString());  // name
    EXPECT_TRUE(info[1].IsInteger());     // arity
    EXPECT_TRUE(info[2].IsArray());       // flags
    EXPECT_TRUE(info[3].IsInteger());     // first_key
    EXPECT_TRUE(info[4].IsInteger());     // last_key
    EXPECT_TRUE(info[5].IsInteger());     // step
    EXPECT_TRUE(info[6].IsArray());       // categories
    EXPECT_TRUE(info[7].IsArray());       // tips
    EXPECT_TRUE(info[8].IsArray());       // key specs
    EXPECT_TRUE(info[9].IsArray());       // subcommands
  }
}

TEST_F(CommandHandlerTest, CommandKeySpecs) {
  // COMMAND INFO GET - verify key specifications
  Command cmd;
  cmd.name = "COMMAND";
  cmd.args.emplace_back(std::string("INFO"));
  cmd.args.emplace_back(std::string("GET"));

  auto result = registry_->Execute(cmd, context_.get());
  ASSERT_TRUE(result.success);
  ASSERT_TRUE(result.response.IsArray());
  
  const auto& cmd_info = result.response.AsArray()[0].AsArray();
  const auto& key_specs = cmd_info[8].AsArray();
  
  // GET should have at least one key spec
  ASSERT_GT(key_specs.size(), 0);
  
  // Verify key spec structure
  const auto& spec = key_specs[0].AsArray();
  ASSERT_EQ(spec.size(), 3);  // flags, begin_search, find_keys
  
  // flags should be an array
  ASSERT_TRUE(spec[0].IsArray());
  const auto& spec_flags = spec[0].AsArray();
  ASSERT_GT(spec_flags.size(), 0);
  EXPECT_TRUE(spec_flags[0].AsString() == "RO" || spec_flags[0].AsString() == "RW");
  
  // begin_search should be an array with type and spec
  ASSERT_TRUE(spec[1].IsArray());
  
  // find_keys should be an array with type and spec
  ASSERT_TRUE(spec[2].IsArray());
}

}  // namespace astra::commands