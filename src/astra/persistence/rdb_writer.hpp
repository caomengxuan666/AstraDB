// ==============================================================================
// RDB Writer - Redis RDB Format Persistence (with Abseil CRC32C)
// ==============================================================================
// License: Apache 2.0
// ==============================================================================
// Design Principles:
// - Streaming checksum calculation with absl::crc32c
// - Memory-efficient - no need to buffer entire file
// - Redis RDB v10 compatible format
// ==============================================================================

#pragma once

#include <absl/crc/crc32c.h>
#include <absl/functional/any_invocable.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/string_view.h>
#include <absl/synchronization/mutex.h>
#include <absl/time/time.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "astra/base/logging.hpp"

namespace astra::persistence {

// RDB opcodes
constexpr uint8_t RDB_OPCODE_EOF = 0xFF;
constexpr uint8_t RDB_OPCODE_SELECTDB = 0xFE;
constexpr uint8_t RDB_OPCODE_RESIZEDB = 0xFB;
constexpr uint8_t RDB_OPCODE_EXPIRETIME_MS = 0xFD;
constexpr uint8_t RDB_OPCODE_AUX = 0xFA;

// RDB types
constexpr uint8_t RDB_TYPE_STRING = 0;
constexpr uint8_t RDB_TYPE_HASH = 1;
constexpr uint8_t RDB_TYPE_LIST = 2;
constexpr uint8_t RDB_TYPE_SET = 3;
constexpr uint8_t RDB_TYPE_ZSET = 4;
constexpr uint8_t RDB_TYPE_HASH_ZIPMAP = 9;
constexpr uint8_t RDB_TYPE_ZSET_ZIPLIST = 11;
constexpr uint8_t RDB_TYPE_HASH_ZIPLIST = 13;
constexpr uint8_t RDB_TYPE_LIST_ZIPLIST = 14;
constexpr uint8_t RDB_TYPE_SET_INTSET = 15;
constexpr uint8_t RDB_TYPE_ZSET_SKIPLIST = 17;
constexpr uint8_t RDB_TYPE_HASH_LISTPACK = 18;

// RDB version
constexpr uint8_t RDB_VERSION = 10;

// RDB configuration options
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
  ~RdbWriter() noexcept { Stop(); }

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

  // Stop RDB writer
  void Stop() noexcept { current_file_.reset(); }

  // Save snapshot to RDB file
  bool Save(absl::AnyInvocable<void(RdbWriter&)> save_callback) noexcept {
    if (!initialized_.load(std::memory_order_acquire)) {
      return false;
    }

    // Create temporary file
    std::string temp_path = options_.save_path + ".tmp";
    current_file_ =
        std::make_unique<std::ofstream>(temp_path, std::ios::binary);
    if (!current_file_->is_open()) {
      ASTRADB_LOG_ERROR("Failed to create temporary RDB file: {}", temp_path);
      current_file_.reset();
      return false;
    }

    // Initialize CRC32C for streaming checksum calculation
    crc_value_ = absl::crc32c_t{0};

    // Write RDB header
    WriteString("REDIS");
    Write(&RDB_VERSION, 1);

    // Write auxiliary fields
    WriteAux("redis-ver", "6.0.0");
    WriteAux("redis-bits", "64");
    WriteAux("ctime", absl::StrCat(absl::ToUnixSeconds(absl::Now())));

    // Call callback to serialize data
    save_callback(*this);

    // Write EOF opcode
    uint8_t eof = RDB_OPCODE_EOF;
    Write(&eof, 1);

    // Write checksum if enabled
    if (options_.checksum) {
      uint32_t checksum = static_cast<uint32_t>(crc_value_);
      current_file_->write(reinterpret_cast<const char*>(&checksum), 4);
    }

    current_file_->close();
    current_file_.reset();

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
  void SelectDb(int db_num) noexcept {
    if (!current_file_ || !current_file_->is_open()) return;
    uint8_t opcode = RDB_OPCODE_SELECTDB;
    Write(&opcode, 1);
    WriteLength(db_num);
  }

  // Write resize database opcode
  void ResizeDb(uint64_t db_size, uint64_t expires_size) noexcept {
    if (!current_file_ || !current_file_->is_open()) return;
    uint8_t opcode = RDB_OPCODE_RESIZEDB;
    Write(&opcode, 1);
    WriteLength(db_size);
    WriteLength(expires_size);
  }

  // Write key-value pair
  void WriteKv(uint8_t type, const std::string& key, const std::string& value,
               int64_t expire_ms = -1) noexcept {
    if (!current_file_ || !current_file_->is_open()) return;

    if (expire_ms >= 0) {
      uint8_t opcode = RDB_OPCODE_EXPIRETIME_MS;
      Write(&opcode, 1);
      uint64_t exp = static_cast<uint64_t>(expire_ms);
      Write(reinterpret_cast<const char*>(&exp), 8);
    }

    Write(&type, 1);
    WriteString(key);
    WriteString(value);
  }

 private:
  // Write data to file with CRC32C update (streaming)
  void Write(const void* data, size_t len) noexcept {
    if (!current_file_ || !current_file_->is_open()) return;

    // Update CRC32C in streaming fashion
    crc_value_ = absl::ExtendCrc32c(
        crc_value_, absl::string_view(static_cast<const char*>(data), len));

    // Write to file
    current_file_->write(static_cast<const char*>(data), len);
  }

  // Write string
  void WriteString(const std::string& str) noexcept {
    WriteLength(str.size());
    Write(str.data(), str.size());
  }

  // Write length in RDB encoding format
  void WriteLength(uint64_t len) noexcept {
    if (len < 0x40) {
      uint8_t b = static_cast<uint8_t>(len);
      Write(&b, 1);
    } else if (len < 0x4000) {
      uint16_t v = static_cast<uint16_t>(len) | 0x4000;
      uint8_t b1 = v & 0xFF;
      uint8_t b2 = (v >> 8) & 0xFF;
      Write(&b1, 1);
      Write(&b2, 1);
    } else {
      uint8_t b = 0x80;
      Write(&b, 1);
      Write(&len, 8);
    }
  }

  // Write auxiliary field
  void WriteAux(const std::string& key, const std::string& value) noexcept {
    uint8_t opcode = RDB_OPCODE_AUX;
    Write(&opcode, 1);
    WriteString(key);
    WriteString(value);
  }

  RdbOptions options_;
  std::atomic<bool> initialized_{false};
  std::unique_ptr<std::ofstream> current_file_;
  absl::crc32c_t crc_value_{absl::crc32c_t{0}};  // CRC32C checksum value
};

}  // namespace astra::persistence
