// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <optional>
#include <string_view>

#include "resp_types.hpp"

namespace astra::protocol {

// RESP Parser - Parse RESP2/RESP3 protocol
class RespParser {
 public:
  RespParser() = default;
  ~RespParser() = default;

  // Parse a complete RESP value from a string view
  // Returns the parsed value and advances the view
  // Returns nullopt if parsing fails
  static std::optional<RespValue> Parse(std::string_view& data);

  // Parse a command from an array (RESP array format)
  // The first element is the command name, rest are arguments
  static std::optional<Command> ParseCommand(const RespValue& value);

  // Check if data contains a complete RESP value
  static bool HasCompleteValue(std::string_view data);

 private:
  // Parse simple string: "+OK\r\n"
  static std::optional<RespValue> ParseSimpleString(std::string_view& data);

  // Parse error: "-Error message\r\n"
  static std::optional<RespValue> ParseError(std::string_view& data);

  // Parse integer: ":1000\r\n"
  static std::optional<RespValue> ParseInteger(std::string_view& data);

  // Parse bulk string: "$6\r\nfoobar\r\n"
  static std::optional<RespValue> ParseBulkString(std::string_view& data);

  // Parse array: "*2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n"
  static std::optional<RespValue> ParseArray(std::string_view& data);

  // Parse RESP3 null: "_\r\n"
  static std::optional<RespValue> ParseNull(std::string_view& data);

  // Parse RESP3 boolean: "#t\r\n" or "#f\r\n"
  static std::optional<RespValue> ParseBoolean(std::string_view& data);

  // Parse RESP3 double: ",3.14\r\n"
  static std::optional<RespValue> ParseDoubleValue(std::string_view& data);

  // Helper: Read until CRLF
  static std::optional<std::string_view> ReadUntilCRLF(std::string_view& data);

  // Helper: Parse integer from string view
  static std::optional<int64_t> ParseInt(std::string_view str);

  // Helper: Parse double from string view
  static std::optional<double> ParseDouble(std::string_view str);
};

}  // namespace astra::protocol
