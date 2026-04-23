// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "resp_parser.hpp"

#include <absl/container/inlined_vector.h>
#include <absl/strings/numbers.h>

#include <algorithm>
#include <charconv>
#include <limits>

#include "astra/base/simd_utils.hpp"

namespace astra::protocol {

namespace {

std::optional<CommandArg> ToCommandArg(RespValue&& value) {
  if (value.IsBulkString()) {
    return CommandArg(std::move(value.MutableString()), RespType::kBulkString);
  }
  if (value.IsSimpleString()) {
    return CommandArg(std::move(value.MutableString()),
                      RespType::kSimpleString);
  }
  if (value.IsInteger()) {
    return CommandArg(value.AsInteger());
  }
  if (value.IsNull()) {
    return CommandArg(RespType::kNullBulkString);
  }
  return std::nullopt;
}

std::optional<CommandArg> ToCommandArg(const RespValue& value) {
  if (value.IsBulkString()) {
    return CommandArg(value.AsString(), RespType::kBulkString);
  }
  if (value.IsSimpleString()) {
    return CommandArg(value.AsString(), RespType::kSimpleString);
  }
  if (value.IsInteger()) {
    return CommandArg(value.AsInteger());
  }
  if (value.IsNull()) {
    return CommandArg(RespType::kNullBulkString);
  }
  return std::nullopt;
}

inline bool ParseIntLineFast(std::string_view& input, int64_t& out,
                             bool allow_negative) noexcept {
  if (input.empty()) {
    return false;
  }

  const char* ptr = input.data();
  const char* end = ptr + input.size();
  bool negative = false;

  if (*ptr == '-') {
    if (!allow_negative) {
      return false;
    }
    negative = true;
    ++ptr;
    if (ptr == end) {
      return false;
    }
  }

  if (*ptr < '0' || *ptr > '9') {
    return false;
  }

  int64_t value = 0;
  while (ptr < end) {
    const char c = *ptr++;
    if (c == '\r') {
      if (ptr >= end || *ptr != '\n') {
        return false;
      }
      ++ptr;
      out = negative ? -value : value;
      input.remove_prefix(static_cast<size_t>(ptr - input.data()));
      return true;
    }

    if (c < '0' || c > '9') {
      return false;
    }

    constexpr int64_t kMaxBeforeMul = std::numeric_limits<int64_t>::max() / 10;
    if (value > kMaxBeforeMul) {
      return false;
    }
    value = value * 10 + static_cast<int64_t>(c - '0');
    if (value < 0) {
      return false;
    }
  }

  return false;
}

}  // namespace

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
    auto arg = ToCommandArg(arr[i]);
    if (!arg.has_value()) {
      return std::nullopt;
    }
    cmd.args.push_back(std::move(*arg));
  }

  return cmd;
}

std::optional<Command> RespParser::ParseCommand(RespValue&& value) {
  if (!value.IsArray() || value.ArraySize() == 0) {
    return std::nullopt;
  }

  auto& arr = value.MutableArray();

  // First element is the command name
  if (!arr[0].IsBulkString()) {
    return std::nullopt;
  }

  Command cmd;
  cmd.name = std::move(arr[0].MutableString());

  // Rest are arguments
  cmd.args.reserve(arr.size() - 1);
  for (size_t i = 1; i < arr.size(); ++i) {
    auto arg = ToCommandArg(std::move(arr[i]));
    if (!arg.has_value()) {
      return std::nullopt;
    }
    cmd.args.push_back(std::move(*arg));
  }

  return cmd;
}

std::optional<Command> RespParser::ParseCommandFromArray(
    std::string_view& data) {
  if (data.empty() || data[0] != '*') {
    return std::nullopt;
  }

  std::string_view working = data;
  working.remove_prefix(1);  // Remove array type byte

  int64_t count64 = 0;
  if (!ParseIntLineFast(working, count64, false)) {
    return std::nullopt;
  }
  if (count64 <= 0 || count64 > std::numeric_limits<int>::max()) {
    return std::nullopt;
  }
  const int count = static_cast<int>(count64);

  Command cmd;
  cmd.args.reserve(static_cast<size_t>(count > 1 ? count - 1 : 0));

  for (int i = 0; i < count; ++i) {
    // Fast path: benchmark workloads are overwhelmingly bulk-string arrays.
    if (!working.empty() && working[0] == '$') {
      working.remove_prefix(1);  // Remove bulk-string type byte

      int64_t bulk_len64 = 0;
      if (!ParseIntLineFast(working, bulk_len64, true)) {
        return std::nullopt;
      }

      const bool is_null_bulk = bulk_len64 == -1;
      if (i == 0) {
        if (is_null_bulk) {
          return std::nullopt;
        }
      } else if (is_null_bulk) {
        cmd.args.emplace_back(RespType::kNullBulkString);
        continue;
      }

      if (bulk_len64 < 0) {
        return std::nullopt;
      }

      const size_t bulk_len = static_cast<size_t>(bulk_len64);
      if (working.size() < bulk_len + 2) {
        return std::nullopt;
      }

      if (working[bulk_len] != '\r' || working[bulk_len + 1] != '\n') {
        return std::nullopt;
      }

      if (i == 0) {
        cmd.name.assign(working.data(), bulk_len);
      } else {
        cmd.args.emplace_back(std::string(working.data(), bulk_len));
      }
      working.remove_prefix(bulk_len + 2);  // Remove payload + CRLF
      continue;
    }

    auto value = Parse(working);
    if (!value.has_value()) {
      return std::nullopt;
    }

    if (i == 0) {
      if (!value->IsBulkString()) {
        return std::nullopt;
      }
      cmd.name = std::move(value->MutableString());
    } else {
      auto arg = ToCommandArg(std::move(*value));
      if (!arg.has_value()) {
        return std::nullopt;
      }
      cmd.args.push_back(std::move(*arg));
    }
  }

  data = working;
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

std::optional<std::string_view> RespParser::ReadUntilCRLF(
    std::string_view& data) {
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
