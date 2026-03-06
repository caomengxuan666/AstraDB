// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <absl/container/flat_hash_map.h>
#include <absl/types/variant.h>
#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace astra::protocol {

// RESP Data Types
enum class RespType : uint32_t {
  kSimpleString = '+',  // Simple String: "+OK\r\n"
  kError = '-',          // Error: "-Error message\r\n"
  kInteger = ':',       // Integer: ":1000\r\n"
  kBulkString = '$',    // Bulk String: "$6\r\nfoobar\r\n"
  kArray = '*',         // Array: "*2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n"
  
  // Special values (not single characters)
  kNullBulkString = 1000,
  kNullArray = 1001,
  
  // RESP3 Only
  kBoolean = '#',       // Boolean: "#t\r\n" or "#f\r\n"
  kDouble = ',',        // Double: ",3.14159265358979323846\r\n"
  kBignum = '(',        // Big Integer: "(34928903284092385093248509738450\r\n"
  kVerbatimString = '=', // Verbatim String: "=15\r\ntxt:Some string\r\n"
  kMap = '%',           // Map: "%2\r\n+key1\r\n+value1\r\n+key2\r\n+value2\r\n"
  kSet = '~',           // Set: "~5\r\n+apple\r\n+banana\r\n+cherry\r\n"
  kPush = '>',          // Push: ">4\r\n+pub/sub\r\n+message\r\n+channel\r\n+hello\r\n"
  kStreamedString = '!', // Streamed String: "...\r\n"
  kAttribute = '|',     // Attribute: "|1\r\n-key\r\n5\r\nvalue\r\n"
  kNull = '_'           // Null: "_\r\n"
};

// RESP Value Variant
class RespValue {
 public:
  // Constructors
  RespValue() : type_(RespType::kNullBulkString) {}
  
  explicit RespValue(RespType type) : type_(type) {}
  
  explicit RespValue(std::string str) 
      : type_(RespType::kBulkString), value_(std::move(str)) {}
  
  explicit RespValue(int64_t num)
      : type_(RespType::kInteger), value_(num) {}
  
  explicit RespValue(double num)
      : type_(RespType::kDouble), value_(num) {}
  
  explicit RespValue(bool b)
      : type_(RespType::kBoolean), value_(b) {}
  
  explicit RespValue(std::vector<RespValue> arr)
      : type_(RespType::kArray), value_(std::move(arr)) {}
  
  explicit RespValue(absl::flat_hash_map<std::string, RespValue> map)
      : type_(RespType::kMap), value_(std::move(map)) {}
  
  // Getters
  RespType GetType() const noexcept { return type_; }

  bool IsNull() const noexcept {
    return type_ == RespType::kNull ||
           type_ == RespType::kNullBulkString ||
           type_ == RespType::kNullArray;
  }
  bool IsSimpleString() const noexcept { return type_ == RespType::kSimpleString; }
  bool IsError() const noexcept { return type_ == RespType::kError; }
  bool IsInteger() const noexcept { return type_ == RespType::kInteger; }
  bool IsBulkString() const noexcept { return type_ == RespType::kBulkString; }
  bool IsArray() const noexcept { return type_ == RespType::kArray; }
  
  // Accessors
  const std::string& AsString() const noexcept {
    if (!std::holds_alternative<std::string>(value_)) {
      // Return empty string for wrong type (fallback)
      static const std::string empty_str;
      return empty_str;
    }
    return absl::get<std::string>(value_);
  }
  
  int64_t AsInteger() const noexcept {
    if (!std::holds_alternative<int64_t>(value_)) {
      return 0;
    }
    return absl::get<int64_t>(value_);
  }
  
  double AsDouble() const noexcept {
    if (!std::holds_alternative<double>(value_)) {
      return 0.0;
    }
    return absl::get<double>(value_);
  }
  
  bool AsBoolean() const noexcept {
    if (!std::holds_alternative<bool>(value_)) {
      return false;
    }
    return absl::get<bool>(value_);
  }
  
  const std::vector<RespValue>& AsArray() const noexcept {
    if (!std::holds_alternative<std::vector<RespValue>>(value_)) {
      static const std::vector<RespValue> empty_arr;
      return empty_arr;
    }
    return absl::get<std::vector<RespValue>>(value_);
  }
  
  const absl::flat_hash_map<std::string, RespValue>& AsMap() const {
    if (!std::holds_alternative<absl::flat_hash_map<std::string, RespValue>>(value_)) {
      static const absl::flat_hash_map<std::string, RespValue> empty_map;
      return empty_map;
    }
    return absl::get<absl::flat_hash_map<std::string, RespValue>>(value_);
  }
  
  // Setters
  void SetString(std::string str, RespType type = RespType::kBulkString) noexcept {
    type_ = type;
    value_ = std::move(str);
  }
  
  void SetInteger(int64_t num) noexcept {
    type_ = RespType::kInteger;
    value_ = num;
  }
  
  void SetArray(std::vector<RespValue> arr) noexcept {
    type_ = RespType::kArray;
    value_ = std::move(arr);
  }
  
  // Size helpers
  size_t ArraySize() const noexcept {
    if (!IsArray()) return 0;
    return AsArray().size();
  }
  
  bool IsEmpty() const noexcept {
    if (IsNull()) return true;
    if (IsBulkString()) return AsString().empty();
    if (IsArray()) return AsArray().empty();
    return false;
  }
  
 private:
  RespType type_;
  absl::variant<std::monostate, std::string, int64_t, double, bool,
                 std::vector<RespValue>, absl::flat_hash_map<std::string, RespValue>> value_;
};

// Command: Parsed command with arguments
struct Command {
  std::string name;
  absl::InlinedVector<RespValue, 4> args;
  
  size_t ArgCount() const { return args.size(); }
  
  const RespValue& operator[](size_t index) const {
    return args[index];
  }
};

}  // namespace astra::protocol