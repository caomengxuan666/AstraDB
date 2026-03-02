// ==============================================================================
// Script Commands Unit Tests (Lua Scripting)
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include <gtest/gtest.h>
#include "astra/commands/script_commands.hpp"
#include "astra/commands/database.hpp"

namespace astra::commands {

class ScriptCommandsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    db_ = std::make_unique<Database>();
    script_ctx_ = std::make_unique<LuaScriptContext>(db_.get());
  }

  std::unique_ptr<Database> db_;
  std::unique_ptr<LuaScriptContext> script_ctx_;
};

TEST_F(ScriptCommandsTest, SimpleStringReturn) {
  std::string script = "return 'Hello World'";
  auto result = script_ctx_->Execute(script, {}, {});
  
  EXPECT_TRUE(result.success);
  EXPECT_FALSE(result.response.IsNull());
  EXPECT_EQ(result.response.AsString(), "Hello World");
}

TEST_F(ScriptCommandsTest, NumberReturn) {
  std::string script = "return 42";
  auto result = script_ctx_->Execute(script, {}, {});
  
  EXPECT_TRUE(result.success);
  EXPECT_EQ(result.response.AsInteger(), 42);
}

TEST_F(ScriptCommandsTest, Arithmetic) {
  std::string script = "return tonumber(ARGV[1]) + tonumber(ARGV[2])";
  auto result = script_ctx_->Execute(script, {}, {"10", "20"});
  
  EXPECT_TRUE(result.success);
  EXPECT_EQ(result.response.AsInteger(), 30);
}

TEST_F(ScriptCommandsTest, StringArrayReturn) {
  std::string script = "return {'a', 'b', 'c'}";
  auto result = script_ctx_->Execute(script, {}, {});
  
  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsArray());
  
  auto arr = result.response.AsArray();
  EXPECT_EQ(arr.size(), 3);
  EXPECT_EQ(arr[0].AsString(), "a");
  EXPECT_EQ(arr[1].AsString(), "b");
  EXPECT_EQ(arr[2].AsString(), "c");
}

TEST_F(ScriptCommandsTest, NumberArrayReturn) {
  std::string script = "return {1, 2, 3}";
  auto result = script_ctx_->Execute(script, {}, {});
  
  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsArray());
  
  auto arr = result.response.AsArray();
  EXPECT_EQ(arr.size(), 3);
  EXPECT_EQ(arr[0].AsInteger(), 1);
  EXPECT_EQ(arr[1].AsInteger(), 2);
  EXPECT_EQ(arr[2].AsInteger(), 3);
}

TEST_F(ScriptCommandsTest, MixedArrayReturn) {
  std::string script = "return {'hello', 42, true}";
  auto result = script_ctx_->Execute(script, {}, {});
  
  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsArray());
  
  auto arr = result.response.AsArray();
  EXPECT_EQ(arr.size(), 3);
  EXPECT_EQ(arr[0].AsString(), "hello");
  EXPECT_EQ(arr[1].AsInteger(), 42);
  EXPECT_EQ(arr[2].AsInteger(), 1);  // true is 1
}

TEST_F(ScriptCommandsTest, KEYSAndARGV) {
  std::string script = "return {KEYS[1], ARGV[1], ARGV[2]}";
  auto result = script_ctx_->Execute(script, {"mykey"}, {"value1", "value2"});
  
  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsArray());
  
  auto arr = result.response.AsArray();
  EXPECT_EQ(arr.size(), 3);
  EXPECT_EQ(arr[0].AsString(), "mykey");
  EXPECT_EQ(arr[1].AsString(), "value1");
  EXPECT_EQ(arr[2].AsString(), "value2");
}

TEST_F(ScriptCommandsTest, EmptyReturn) {
  std::string script = "return nil";
  auto result = script_ctx_->Execute(script, {}, {});
  
  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.response.IsNull());
}

TEST_F(ScriptCommandsTest, SingleElementArray) {
  std::string script = "return {'single'}";
  auto result = script_ctx_->Execute(script, {}, {});
  
  EXPECT_TRUE(result.success);
  // Single element array should be returned as single element
  EXPECT_FALSE(result.response.IsNull());
  EXPECT_FALSE(result.response.IsArray());
  EXPECT_EQ(result.response.AsString(), "single");
}

TEST_F(ScriptCommandsTest, BooleanReturn) {
  std::string script = "return true";
  auto result = script_ctx_->Execute(script, {}, {});
  
  EXPECT_TRUE(result.success);
  EXPECT_EQ(result.response.AsInteger(), 1);
}

TEST_F(ScriptCommandsTest, BooleanFalseReturn) {
  std::string script = "return false";
  auto result = script_ctx_->Execute(script, {}, {});
  
  EXPECT_TRUE(result.success);
  EXPECT_EQ(result.response.AsInteger(), 0);
}

TEST_F(ScriptCommandsTest, SyntaxError) {
  std::string script = "return 'unclosed string";
  auto result = script_ctx_->Execute(script, {}, {});
  
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.error.empty());
}

TEST_F(ScriptCommandsTest, RuntimeError) {
  std::string script = "error('test error')";
  auto result = script_ctx_->Execute(script, {}, {});
  
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.error.empty());
}

}  // namespace astra::commands