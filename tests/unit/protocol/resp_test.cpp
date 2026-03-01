#include <gtest/gtest.h>
#include "astra/protocol/resp/resp_parser.hpp"
#include "astra/protocol/resp/resp_builder.hpp"

namespace astra::protocol {

TEST(RespParserTest, ParseSimpleString) {
  std::string_view data = "+OK\r\n";
  auto value = RespParser::Parse(data);
  
  ASSERT_TRUE(value.has_value());
  EXPECT_TRUE(value->IsSimpleString());
  EXPECT_EQ(value->AsString(), "OK");
}

TEST(RespParserTest, ParseError) {
  std::string_view data = "-Error message\r\n";
  auto value = RespParser::Parse(data);
  
  ASSERT_TRUE(value.has_value());
  EXPECT_TRUE(value->IsError());
  EXPECT_EQ(value->AsString(), "Error message");
}

TEST(RespParserTest, ParseInteger) {
  std::string_view data = ":1000\r\n";
  auto value = RespParser::Parse(data);
  
  ASSERT_TRUE(value.has_value());
  EXPECT_TRUE(value->IsInteger());
  EXPECT_EQ(value->AsInteger(), 1000);
}

TEST(RespParserTest, ParseNegativeInteger) {
  std::string_view data = ":-42\r\n";
  auto value = RespParser::Parse(data);
  
  ASSERT_TRUE(value.has_value());
  EXPECT_TRUE(value->IsInteger());
  EXPECT_EQ(value->AsInteger(), -42);
}

TEST(RespParserTest, ParseBulkString) {
  std::string_view data = "$6\r\nfoobar\r\n";
  auto value = RespParser::Parse(data);
  
  ASSERT_TRUE(value.has_value());
  EXPECT_TRUE(value->IsBulkString());
  EXPECT_EQ(value->AsString(), "foobar");
}

TEST(RespParserTest, ParseNullBulkString) {
  std::string_view data = "$-1\r\n";
  auto value = RespParser::Parse(data);
  
  ASSERT_TRUE(value.has_value());
  EXPECT_TRUE(value->IsNull());
}

TEST(RespParserTest, ParseEmptyBulkString) {
  std::string_view data = "$0\r\n\r\n";
  auto value = RespParser::Parse(data);
  
  ASSERT_TRUE(value.has_value());
  EXPECT_TRUE(value->IsBulkString());
  EXPECT_TRUE(value->AsString().empty());
}

TEST(RespParserTest, ParseArray) {
  std::string_view data = "*2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n";
  auto value = RespParser::Parse(data);
  
  ASSERT_TRUE(value.has_value());
  EXPECT_TRUE(value->IsArray());
  EXPECT_EQ(value->ArraySize(), 2);
  
  const auto& arr = value->AsArray();
  EXPECT_EQ(arr[0].AsString(), "foo");
  EXPECT_EQ(arr[1].AsString(), "bar");
}

TEST(RespParserTest, ParseEmptyArray) {
  std::string_view data = "*0\r\n";
  auto value = RespParser::Parse(data);
  
  ASSERT_TRUE(value.has_value());
  EXPECT_TRUE(value->IsArray());
  EXPECT_EQ(value->ArraySize(), 0);
}

TEST(RespParserTest, ParseNullArray) {
  std::string_view data = "*-1\r\n";
  auto value = RespParser::Parse(data);
  
  ASSERT_TRUE(value.has_value());
  EXPECT_TRUE(value->IsNull());
}

TEST(RespParserTest, ParseNestedArray) {
  std::string_view data = "*2\r\n*1\r\n$3\r\nfoo\r\n$3\r\nbar\r\n";
  auto value = RespParser::Parse(data);
  
  ASSERT_TRUE(value.has_value());
  EXPECT_TRUE(value->IsArray());
  EXPECT_EQ(value->ArraySize(), 2);
  
  const auto& arr = value->AsArray();
  EXPECT_TRUE(arr[0].IsArray());
  EXPECT_EQ(arr[0].ArraySize(), 1);
  EXPECT_EQ(arr[0].AsArray()[0].AsString(), "foo");
  EXPECT_EQ(arr[1].AsString(), "bar");
}

TEST(RespParserTest, ParseCommand) {
  std::string_view data = "*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$5\r\nvalue\r\n";
  auto value = RespParser::Parse(data);
  
  ASSERT_TRUE(value.has_value());
  
  auto cmd = RespParser::ParseCommand(*value);
  ASSERT_TRUE(cmd.has_value());
  EXPECT_EQ(cmd->name, "SET");
  EXPECT_EQ(cmd->ArgCount(), 2);
  EXPECT_EQ(cmd->args[0].AsString(), "key");
  EXPECT_EQ(cmd->args[1].AsString(), "value");
}

TEST(RespParserTest, ParseBoolean) {
  std::string_view data = "#t\r\n";
  auto value = RespParser::Parse(data);
  
  ASSERT_TRUE(value.has_value());
  EXPECT_TRUE(value->AsBoolean());
}

TEST(RespParserTest, ParseBooleanFalse) {
  std::string_view data = "#f\r\n";
  auto value = RespParser::Parse(data);
  
  ASSERT_TRUE(value.has_value());
  EXPECT_FALSE(value->AsBoolean());
}

TEST(RespParserTest, ParseNull) {
  std::string_view data = "_\r\n";
  auto value = RespParser::Parse(data);
  
  ASSERT_TRUE(value.has_value());
  EXPECT_TRUE(value->IsNull());
}

TEST(RespBuilderTest, BuildSimpleString) {
  auto out = RespBuilder::BuildSimpleString("OK");
  EXPECT_EQ(out, "+OK\r\n");
}

TEST(RespBuilderTest, BuildError) {
  auto out = RespBuilder::BuildError("ERR unknown command");
  EXPECT_EQ(out, "-ERR unknown command\r\n");
}

TEST(RespBuilderTest, BuildInteger) {
  auto out = RespBuilder::BuildInteger(1000);
  EXPECT_EQ(out, ":1000\r\n");
}

TEST(RespBuilderTest, BuildBulkString) {
  auto out = RespBuilder::BuildBulkString("hello");
  EXPECT_EQ(out, "$5\r\nhello\r\n");
}

TEST(RespBuilderTest, BuildNullBulkString) {
  auto out = RespBuilder::BuildNullBulkString();
  EXPECT_EQ(out, "$-1\r\n");
}

TEST(RespBuilderTest, BuildArray) {
  std::vector<RespValue> arr;
  arr.emplace_back("foo");
  arr.emplace_back("bar");
  
  auto out = RespBuilder::BuildArray(arr);
  EXPECT_EQ(out, "*2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n");
}

TEST(RespBuilderTest, BuildStringArray) {
  std::vector<std::string> arr = {"foo", "bar"};
  auto out = RespBuilder::BuildArray(arr);
  EXPECT_EQ(out, "*2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n");
}

TEST(RespBuilderTest, BuildOK) {
  auto out = RespBuilder::BuildOK();
  EXPECT_EQ(out, "+OK\r\n");
}

TEST(RespBuilderTest, BuildNil) {
  auto out = RespBuilder::BuildNil();
  EXPECT_EQ(out, "$-1\r\n");
}

TEST(RespBuilderTest, BuildPong) {
  auto out = RespBuilder::BuildPong();
  EXPECT_EQ(out, "+PONG\r\n");
}

TEST(RespBuilderTest, BuildBoolean) {
  auto out = RespBuilder::BuildBoolean(true);
  EXPECT_EQ(out, "#t\r\n");
  
  out = RespBuilder::BuildBoolean(false);
  EXPECT_EQ(out, "#f\r\n");
}

TEST(RespBuilderTest, BuildNull) {
  auto out = RespBuilder::BuildNull();
  EXPECT_EQ(out, "_\r\n");
}

TEST(RespBuilderTest, RoundTripSimpleString) {
  RespValue original(RespType::kSimpleString);
  original.SetString("hello");
  
  auto out = RespBuilder::Build(original);
  std::string_view data(out);
  auto parsed = RespParser::Parse(data);
  
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->AsString(), "hello");
}

TEST(RespBuilderTest, RoundTripBulkString) {
  RespValue original("world");
  
  auto out = RespBuilder::Build(original);
  std::string_view data(out);
  auto parsed = RespParser::Parse(data);
  
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->AsString(), "world");
}

TEST(RespBuilderTest, RoundTripArray) {
  std::vector<RespValue> arr;
  arr.emplace_back("foo");
  arr.emplace_back("bar");
  arr.emplace_back(static_cast<int64_t>(42));
  
  RespValue original(std::move(arr));
  
  auto out = RespBuilder::Build(original);
  std::string_view data(out);
  auto parsed = RespParser::Parse(data);
  
  ASSERT_TRUE(parsed.has_value());
  EXPECT_TRUE(parsed->IsArray());
  EXPECT_EQ(parsed->ArraySize(), 3);
  EXPECT_EQ(parsed->AsArray()[0].AsString(), "foo");
  EXPECT_EQ(parsed->AsArray()[1].AsString(), "bar");
  EXPECT_EQ(parsed->AsArray()[2].AsInteger(), 42ll);
}

}  // namespace astra::protocol