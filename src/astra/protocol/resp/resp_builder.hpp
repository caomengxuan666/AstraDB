// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "resp_types.hpp"
#include <string>

namespace astra::protocol {

// RESP Builder - Serialize values to RESP format
class RespBuilder {
 public:
  RespBuilder() = default;
  ~RespBuilder() = default;
  
  // Build RESP value to string
  static std::string Build(const RespValue& value);
  
  // Build simple string: "+OK\r\n"
  static std::string BuildSimpleString(std::string_view str);
  
  // Build error: "-Error message\r\n"
  static std::string BuildError(std::string_view msg);
  
  // Build integer: ":1000\r\n"
  static std::string BuildInteger(int64_t num);
  
  // Build bulk string: "$6\r\nfoobar\r\n"
  static std::string BuildBulkString(std::string_view str);
  
  // Build null bulk string: "$-1\r\n"
  static std::string BuildNullBulkString();
  
  // Build array: "*2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n"
  static std::string BuildArray(const std::vector<RespValue>& arr);
  
  // Build null array: "*-1\r\n"
  static std::string BuildNullArray();
  
  // Build OK response (simple string)
  static std::string BuildOK();
  
  // Build nil response (null bulk string)
  static std::string BuildNil();
  
  // Build pong response
  static std::string BuildPong();
  
  // Build integer response
  static std::string BuildInt(int64_t num);
  
  // Build string response
  static std::string BuildString(std::string_view str);
  
  // Build array response
  static std::string BuildArray(const std::vector<std::string>& arr);
  
  // Build error response
  static std::string BuildError(std::string_view type, std::string_view msg);
  
  // RESP3: Build boolean
  static std::string BuildBoolean(bool b);
  
  // RESP3: Build double
  static std::string BuildDouble(double num);
  
  // RESP3: Build null
  static std::string BuildNull();
  
 private:
  // Helper: Append CRLF
  static void AppendCRLF(std::string& out);
  
  // Helper: Append integer
  static void AppendInteger(std::string& out, int64_t num);
};

}  // namespace astra::protocol