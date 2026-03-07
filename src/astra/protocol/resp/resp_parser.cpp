// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "resp_parser.hpp"
#include "astra/base/simd_utils.hpp"
#include <absl/container/inlined_vector.h>
#include <absl/strings/numbers.h>
#include <charconv>
#include <algorithm>

namespace astra::protocol {

std::optional<RespValue> RespParser::Parse(std::string_view& data) {
  if (data.empty()) {
    return std::nullopt;
  }
  
  char type = data[0];
  data.remove_prefix(1);  // Remove type byte
  
  switch (type) {
    case '+':
      return ParseSimpleString(data);
    case '-':
      return ParseError(data);
    case ':':
      return ParseInteger(data);
    case '$':
      return ParseBulkString(data);
    case '*':
      return ParseArray(data);
    case '_':
      return ParseNull(data);
    case '#':
      return ParseBoolean(data);
    case ',':
      return ParseDoubleValue(data);
    default:
      return std::nullopt;
  }
}

std::optional<Command> RespParser::ParseCommand(const RespValue& value) {
  if (!value.IsArray() || value.ArraySize() == 0) {
    return std::nullopt;
  }
  
  const auto& arr = value.AsArray();
  
  // First element is the command name
  if (!arr[0].IsBulkString()) {
    return std::nullopt;
  }
  
  Command cmd;
  cmd.name = arr[0].AsString();
  
  // Rest are arguments
  for (size_t i = 1; i < arr.size(); ++i) {
    cmd.args.push_back(arr[i]);
  }
  
  return cmd;
}

bool RespParser::HasCompleteValue(std::string_view data) {
  std::string_view copy = data;
  return Parse(copy).has_value();
}

std::optional<RespValue> RespParser::ParseSimpleString(std::string_view& data) {
  auto line = ReadUntilCRLF(data);
  if (!line.has_value()) {
    return std::nullopt;
  }

  RespValue value(RespType::kSimpleString);
  value.SetString(std::string(*line), RespType::kSimpleString);
  return value;
}

std::optional<RespValue> RespParser::ParseError(std::string_view& data) {
  auto line = ReadUntilCRLF(data);
  if (!line.has_value()) {
    return std::nullopt;
  }

  RespValue value(RespType::kError);
  value.SetString(std::string(*line), RespType::kError);
  return value;
}

std::optional<RespValue> RespParser::ParseInteger(std::string_view& data) {
  auto line = ReadUntilCRLF(data);
  if (!line.has_value()) {
    return std::nullopt;
  }
  
  auto num = ParseInt(*line);
  if (!num.has_value()) {
    return std::nullopt;
  }
  
  return RespValue(*num);
}

std::optional<RespValue> RespParser::ParseBulkString(std::string_view& data) {
  auto line = ReadUntilCRLF(data);
  if (!line.has_value()) {
    return std::nullopt;
  }
  
  auto len = ParseInt(*line);
  if (!len.has_value()) {
    return std::nullopt;
  }
  
  if (*len == -1) {
    // Null bulk string
    return RespValue(RespType::kNullBulkString);
  }
  
  if (*len < 0) {
    return std::nullopt;
  }
  
  if (data.size() < static_cast<size_t>(*len) + 2) {
    return std::nullopt;  // Not enough data
  }
  
  std::string str(data.substr(0, *len));
  data.remove_prefix(*len + 2);  // Remove string + CRLF
  
  return RespValue(std::move(str));
}

std::optional<RespValue> RespParser::ParseArray(std::string_view& data) {
  auto line = ReadUntilCRLF(data);
  if (!line.has_value()) {
    return std::nullopt;
  }
  
  auto count = ParseInt(*line);
  if (!count.has_value()) {
    return std::nullopt;
  }
  
  if (*count == -1) {
    // Null array
    return RespValue(RespType::kNullArray);
  }
  
  if (*count < 0) {
    return std::nullopt;
  }
  
  std::vector<RespValue> arr;
  arr.reserve(*count);
  
  for (int i = 0; i < *count; ++i) {
    auto value = Parse(data);
    if (!value.has_value()) {
      return std::nullopt;
    }
    arr.push_back(std::move(*value));
  }
  
  return RespValue(std::move(arr));
}

std::optional<RespValue> RespParser::ParseNull(std::string_view& data) {
  auto line = ReadUntilCRLF(data);
  if (!line.has_value()) {
    return std::nullopt;
  }
  
  if (*line != "") {
    return std::nullopt;
  }
  
  return RespValue(RespType::kNull);
}

std::optional<RespValue> RespParser::ParseBoolean(std::string_view& data) {
  auto line = ReadUntilCRLF(data);
  if (!line.has_value()) {
    return std::nullopt;
  }
  
  if (*line == "t") {
    return RespValue(true);
  } else if (*line == "f") {
    return RespValue(false);
  }
  
  return std::nullopt;
}

std::optional<RespValue> RespParser::ParseDoubleValue(std::string_view& data) {
  auto line = ReadUntilCRLF(data);
  if (!line.has_value()) {
    return std::nullopt;
  }
  
  auto num = ParseDouble(*line);
  if (!num.has_value()) {
    return std::nullopt;
  }
  
  return RespValue(*num);
}

std::optional<std::string_view> RespParser::ReadUntilCRLF(std::string_view& data) {
  // Use SIMD-accelerated CRLF search for large buffers
  const char* crlf_ptr = nullptr;
  
  if (data.size() >= 32) {
    crlf_ptr = astra::base::simd::FindCRLF(data.data(), data.size());
  } else {
    // For small buffers, use the standard approach
    size_t crlf_pos = data.find("\r\n");
    if (crlf_pos == std::string_view::npos) {
      return std::nullopt;
    }
    auto result = data.substr(0, crlf_pos);
    data.remove_prefix(crlf_pos + 2);
    return result;
  }
  
  if (crlf_ptr == nullptr) {
    return std::nullopt;
  }
  
  size_t crlf_pos = crlf_ptr - data.data();
  auto result = data.substr(0, crlf_pos);
  data.remove_prefix(crlf_pos + 2);
  return result;
}

std::optional<int64_t> RespParser::ParseInt(std::string_view str) {
  int64_t result = 0;
  auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), result);
  
  if (ec != std::errc()) {
    return std::nullopt;
  }
  
  return result;
}

std::optional<double> RespParser::ParseDouble(std::string_view str) {
  // Simple atof for now
  std::string temp(str);
  double result;
  try {
    if (!absl::SimpleAtod(temp, &result)) {
      return std::nullopt;
    }
    return result;
  } catch (...) {
    return std::nullopt;
  }
}

}  // namespace astra::protocol
