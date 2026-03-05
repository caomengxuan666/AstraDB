// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <fstream>
#include <absl/functional/any_invocable.h>
#include "astra/base/logging.hpp"

namespace astra::persistence {

// RDB file format constants
constexpr uint8_t RDB_VERSION = 10;
constexpr uint8_t RDB_OPCODE_EOF = 0xFF;
constexpr uint8_t RDB_OPCODE_SELECTDB = 0xFE;
constexpr uint8_t RDB_OPCODE_RESIZEDB = 0xFB;
constexpr uint8_t RDB_OPCODE_AUX = 0xFA;
constexpr uint8_t RDB_OPCODE_EXPIRETIME_MS = 0xFC;
constexpr uint8_t RDB_OPCODE_EXPIRETIME = 0xFD;
constexpr uint8_t RDB_TYPE_STRING = 0;
constexpr uint8_t RDB_TYPE_HASH_ZIPMAP = 9;
constexpr uint8_t RDB_TYPE_HASH_ZIPLIST = 13;
constexpr uint8_t RDB_TYPE_SET_INTSET = 11;
constexpr uint8_t RDB_TYPE_SET_LISTPACK = 14;
constexpr uint8_t RDB_TYPE_ZSET_ZIPLIST = 12;
constexpr uint8_t RDB_TYPE_ZSET_LISTPACK = 15;
constexpr uint8_t RDB_TYPE_LIST_ZIPLIST = 10;
constexpr uint8_t RDB_TYPE_LIST_QUICKLIST = 14;
constexpr uint8_t RDB_TYPE_HASH = 4;
constexpr uint8_t RDB_TYPE_LIST = 3;
constexpr uint8_t RDB_TYPE_SET = 2;
constexpr uint8_t RDB_TYPE_ZSET = 5;
constexpr uint8_t RDB_TYPE_STREAM_LISTPACKS = 16;

// RDB configuration
struct RdbOptions {
  std::string save_path = "./data/dump.rdb";
  bool compress = true;
  int compression_level = 6;  // zstd compression level
  bool checksum = true;
};

// RDB Writer - creates Redis RDB format snapshots
class RdbWriter {
 public:
  RdbWriter() noexcept = default;
  ~RdbWriter() noexcept = default;

  // Non-copyable, non-movable
  RdbWriter(const RdbWriter&) = delete;
  RdbWriter& operator=(const RdbWriter&) = delete;
  RdbWriter(RdbWriter&&) = delete;
  RdbWriter& operator=(RdbWriter&&) = delete;

  // Initialize RDB writer
  bool Init(const RdbOptions& options) noexcept {
    options_ = options;
    
    // Create directory if not exists
    std::filesystem::path p(options_.save_path);
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    if (ec) {
      ASTRADB_LOG_ERROR("Failed to create RDB directory: {}", ec.message());
      return false;
    }
    
    initialized_.store(true, std::memory_order_release);
    return true;
  }

  // Save snapshot to RDB file
  bool Save(absl::AnyInvocable<void(RdbWriter&)> save_callback) noexcept {
    if (!initialized_.load(std::memory_order_acquire)) {
      return false;
    }

    // Create temporary file
    std::string temp_path = options_.save_path + ".tmp";
    std::ofstream file(temp_path, std::ios::binary);
    if (!file.is_open()) {
      ASTRADB_LOG_ERROR("Failed to create temporary RDB file: {}", temp_path);
      return false;
    }

    // Write RDB header
    WriteString(file, "REDIS");
    file.write(reinterpret_cast<const char*>(&RDB_VERSION), 1);

    // Write auxiliary fields
    WriteAux(file, "redis-ver", "6.0.0");
    WriteAux(file, "redis-bits", "64");
    WriteAux(file, "ctime", absl::StrCat(absl::ToUnixSeconds(absl::Now())));

    // Call callback to write database content
    save_callback(*this);

    // Write EOF and checksum
    file.write(reinterpret_cast<const char*>(&RDB_OPCODE_EOF), 1);
    
    if (options_.checksum) {
      uint64_t checksum = 0;  // TODO: Calculate actual checksum
      file.write(reinterpret_cast<const char*>(&checksum), 8);
    }

    file.close();

    // Replace old RDB with new one
    std::filesystem::path temp_p(temp_path);
    std::filesystem::path final_p(options_.save_path);
    std::error_code ec;
    std::filesystem::rename(temp_p, final_p, ec);
    if (ec) {
      ASTRADB_LOG_ERROR("Failed to rename RDB file: {}", ec.message());
      return false;
    }

    ASTRADB_LOG_INFO("RDB snapshot saved: {}", options_.save_path);
    return true;
  }

  // Write select database opcode
  void SelectDb(std::ofstream& file, int db_num) noexcept {
    file.write(reinterpret_cast<const char*>(&RDB_OPCODE_SELECTDB), 1);
    WriteLength(file, db_num);
  }

  // Write resize database opcode
  void ResizeDb(std::ofstream& file, uint64_t db_size, uint64_t expires_size) noexcept {
    file.write(reinterpret_cast<const char*>(&RDB_OPCODE_RESIZEDB), 1);
    WriteLength(file, db_size);
    WriteLength(file, expires_size);
  }

  // Write key-value pair
  void WriteKv(std::ofstream& file, uint8_t type, const std::string& key, const std::string& value, int64_t expire_ms = -1) noexcept {
    if (expire_ms >= 0) {
      file.write(reinterpret_cast<const char*>(&RDB_OPCODE_EXPIRETIME_MS), 1);
      uint64_t exp = static_cast<uint64_t>(expire_ms);
      file.write(reinterpret_cast<const char*>(&exp), 8);
    }
    
    file.write(reinterpret_cast<const char*>(&type), 1);
    WriteString(file, key);
    WriteString(file, value);
  }

 private:
  // Write string to file
  void WriteString(std::ofstream& file, const std::string& str) noexcept {
    WriteLength(file, str.size());
    file.write(str.data(), str.size());
  }

  // Write length in RDB encoding format
  void WriteLength(std::ofstream& file, uint64_t len) noexcept {
    if (len < 0x40) {
      uint8_t b = static_cast<uint8_t>(len);
      file.write(reinterpret_cast<const char*>(&b), 1);
    } else if (len < 0x4000) {
      uint16_t v = static_cast<uint16_t>(len) | 0x4000;
      uint8_t b1 = v & 0xFF;
      uint8_t b2 = (v >> 8) & 0xFF;
      file.write(reinterpret_cast<const char*>(&b1), 1);
      file.write(reinterpret_cast<const char*>(&b2), 1);
    } else {
      uint8_t b = 0x80;
      file.write(reinterpret_cast<const char*>(&b), 1);
      file.write(reinterpret_cast<const char*>(&len), 8);
    }
  }

  // Write auxiliary field
  void WriteAux(std::ofstream& file, const std::string& key, const std::string& value) noexcept {
    file.write(reinterpret_cast<const char*>(&RDB_OPCODE_AUX), 1);
    WriteString(file, key);
    WriteString(file, value);
  }

  RdbOptions options_;
  std::atomic<bool> initialized_{false};
};

}  // namespace astra::persistence