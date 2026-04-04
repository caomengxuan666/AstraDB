// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "resp_builder.hpp"

#include <absl/strings/str_cat.h>

#include <charconv>

namespace astra::protocol {

std::string RespBuilder::Build(const RespValue& value) {
  std::string out;

  switch (value.GetType()) {
    case RespType::kSimpleString:
      out += '+';
      out += value.AsString();
      AppendCRLF(out);
      break;

    case RespType::kError:
      out += '-';
      out += value.AsString();
      AppendCRLF(out);
      break;

    case RespType::kInteger:
      out += ':';
      AppendInteger(out, value.AsInteger());
      AppendCRLF(out);
      break;

    case RespType::kBulkString:
      out += BuildBulkString(value.AsString());
      break;

    case RespType::kArray:
      out += BuildArray(value.AsArray());
      break;

    case RespType::kNullBulkString:
      out += BuildNullBulkString();
      break;

    case RespType::kNullArray:
      out += BuildNullArray();
      break;

    case RespType::kBoolean:
      out += BuildBoolean(value.AsBoolean());
      break;

    case RespType::kDouble:
      out += BuildDouble(value.AsDouble());
      break;

    case RespType::kNull:
      out += BuildNull();
      break;

    case RespType::kMap:
      out += BuildMap(value.AsMap());
      break;

    default:
      out += BuildNil();
      break;
  }

  return out;
}

std::string RespBuilder::BuildSimpleString(std::string_view str) {
  std::string out;
  out += '+';
  out += str;
  AppendCRLF(out);
  return out;
}

std::string RespBuilder::BuildError(std::string_view msg) {
  std::string out;
  out += '-';
  out += msg;
  AppendCRLF(out);
  return out;
}

std::string RespBuilder::BuildInteger(int64_t num) {
  std::string out;
  out += ':';
  AppendInteger(out, num);
  AppendCRLF(out);
  return out;
}

std::string RespBuilder::BuildBulkString(std::string_view str) {
  std::string out;
  out += '$';
  AppendInteger(out, static_cast<int64_t>(str.size()));
  AppendCRLF(out);
  out += str;
  AppendCRLF(out);
  return out;
}

std::string RespBuilder::BuildNullBulkString() { return "$-1\r\n"; }

std::string RespBuilder::BuildArray(const std::vector<RespValue>& arr) {
  std::string out;
  out += '*';
  AppendInteger(out, static_cast<int64_t>(arr.size()));
  AppendCRLF(out);

  for (const auto& elem : arr) {
    out += Build(elem);
  }

  return out;
}

std::string RespBuilder::BuildNullArray() { return "*-1\r\n"; }

std::string RespBuilder::BuildOK() { return "+OK\r\n"; }

std::string RespBuilder::BuildNil() { return BuildNullBulkString(); }

std::string RespBuilder::BuildPong() { return "+PONG\r\n"; }

std::string RespBuilder::BuildInt(int64_t num) { return BuildInteger(num); }

std::string RespBuilder::BuildString(std::string_view str) {
  return BuildBulkString(str);
}

std::string RespBuilder::BuildArray(const std::vector<std::string>& arr) {
  std::string out;
  out += '*';
  AppendInteger(out, static_cast<int64_t>(arr.size()));
  AppendCRLF(out);

  for (const auto& elem : arr) {
    out += BuildBulkString(elem);
  }

  return out;
}

std::string RespBuilder::BuildError(std::string_view type,
                                    std::string_view msg) {
  std::string out;
  out += '-';
  out += type;
  out += ' ';
  out += msg;
  AppendCRLF(out);
  return out;
}

std::string RespBuilder::BuildMoved(uint16_t slot, const std::string& ip,
                                    uint16_t port) {
  std::string out;
  out += '-';
  out += "MOVED ";
  AppendInteger(out, static_cast<int64_t>(slot));
  out += ' ';
  out += ip;
  out += ':';
  AppendInteger(out, static_cast<int64_t>(port));
  AppendCRLF(out);
  return out;
}

std::string RespBuilder::BuildBoolean(bool b) {
  std::string out;
  out += '#';
  out += (b ? 't' : 'f');
  AppendCRLF(out);
  return out;
}

std::string RespBuilder::BuildDouble(double num) {
  std::string out;
  out += ',';
  // Simple double conversion
  out += absl::StrCat(num);
  AppendCRLF(out);
  return out;
}

std::string RespBuilder::BuildNull() { return "_\r\n"; }

std::string RespBuilder::BuildMap(
    const absl::flat_hash_map<std::string, RespValue>& map) {
  std::string out;
  out += '%';
  AppendInteger(out, static_cast<int64_t>(map.size()));
  AppendCRLF(out);

  for (const auto& [key, value] : map) {
    out += BuildBulkString(key);
    out += Build(value);
  }

  return out;
}

void RespBuilder::AppendCRLF(std::string& out) { out += "\r\n"; }

void RespBuilder::AppendInteger(std::string& out, int64_t num) {
  out += absl::StrCat(num);
}

}  // namespace astra::protocol
