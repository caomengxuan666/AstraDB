// ==============================================================================
// RDB Reader - Redis RDB Format Persistence (Read)
// ==============================================================================
// License: Apache 2.0
// ==============================================================================
// Design Principles:
// - Streaming checksum verification with absl::crc32c
// - Memory-efficient - no need to buffer entire file
// - Redis RDB v10 compatible format
// ==============================================================================

#pragma once

#include <absl/crc/crc32c.h>
#include <absl/strings/numbers.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/string_view.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>

#include "astra/base/logging.hpp"
#include "astra/persistence/rdb_common.hpp"

namespace astra::persistence {

// RDB Reader - reads Redis RDB format snapshots
class RdbReader {
 public:
  RdbReader() noexcept = default;
  ~RdbReader() noexcept { Stop(); }

  // Non-copyable, non-movable
  RdbReader(const RdbReader&) = delete;
  RdbReader& operator=(const RdbReader&) = delete;
  RdbReader(RdbReader&&) = delete;
  RdbReader& operator=(RdbReader&&) = delete;

  // Initialize RDB reader
  bool Init(const std::string& file_path, bool verify_checksum = true) noexcept {
    file_path_ = file_path;
    verify_checksum_ = verify_checksum;

    // Check if file exists
    if (!std::filesystem::exists(file_path)) {
      ASTRADB_LOG_WARN("RDB file not found: {}", file_path);
      return false;
    }

    initialized_.store(true, std::memory_order_release);
    return true;
  }

  // Stop RDB reader
  void Stop() noexcept { current_file_.reset(); }

  // Load RDB file and call callback for each key-value pair
  bool Load(const std::function<void(int, const RdbKeyValue&)>& load_callback) noexcept {
    if (!initialized_.load(std::memory_order_acquire)) {
      return false;
    }

    current_file_ = std::make_unique<std::ifstream>(file_path_, std::ios::binary);
    if (!current_file_->is_open()) {
      ASTRADB_LOG_ERROR("Failed to open RDB file: {}", file_path_);
      current_file_.reset();
      return false;
    }

    // Initialize CRC32C for streaming checksum calculation
    crc_value_ = absl::crc32c_t{0};

    // Read and verify RDB header
    if (!ReadHeader()) {
      current_file_->close();
      current_file_.reset();
      return false;
    }

    // Read auxiliary fields and get first opcode
    uint8_t opcode = ReadAuxFields();

    int current_db = 0;
    uint64_t db_size = 0;
    uint64_t expires_size = 0;

    // Read database sections
    while (true) {
      if (opcode == RDB_OPCODE_EOF) {
        ASTRADB_LOG_DEBUG("RDB EOF read, current CRC32C: {}",
                         static_cast<uint32_t>(crc_value_));
        break;
      }

      if (opcode == RDB_OPCODE_SELECTDB) {
        current_db = ReadLength();
        ASTRADB_LOG_DEBUG("RDB: Selected database {}", current_db);
      } else if (opcode == RDB_OPCODE_RESIZEDB) {
        db_size = ReadLength();
        expires_size = ReadLength();
        ASTRADB_LOG_DEBUG("RDB: Database {} has {} keys, {} expires", 
                         current_db, db_size, expires_size);
      } else {
        // It's a value type
        int64_t expire_ms = -1;

        // Check for expiration opcode
        if (opcode == RDB_OPCODE_EXPIRETIME_MS) {
          uint64_t exp = ReadUint64();
          expire_ms = static_cast<int64_t>(exp);
          // Read type byte
          uint8_t type = ReadOpcode();
          ReadKeyValue(type, expire_ms, current_db, load_callback);
        } else {
          // opcode is actually the type byte
          ReadKeyValue(opcode, -1, current_db, load_callback);
        }
      }

      // Read next opcode
      opcode = ReadOpcode();
    }

    // Read and verify checksum
    if (verify_checksum_) {
      if (!VerifyChecksum()) {
        current_file_->close();
        current_file_.reset();
        return false;
      }
    }

    current_file_->close();
    current_file_.reset();

    ASTRADB_LOG_INFO("RDB snapshot loaded: {}", file_path_);
    return true;
  }

 private:
  // Read data from file with CRC32C update (streaming)
  bool Read(void* data, size_t len) noexcept {
    if (!current_file_ || !current_file_->is_open()) return false;

    current_file_->read(static_cast<char*>(data), len);
    if (!current_file_->good() && !current_file_->eof()) {
      return false;
    }

    // Update CRC32C in streaming fashion
    crc_value_ = absl::ExtendCrc32c(
        crc_value_, absl::string_view(static_cast<const char*>(data), len));

    return true;
  }

  // Read and verify RDB header
  bool ReadHeader() noexcept {
    char magic[5];
    if (!Read(magic, 5)) {
      ASTRADB_LOG_ERROR("Failed to read RDB magic string");
      return false;
    }

    if (std::string(magic, 5) != "REDIS") {
      ASTRADB_LOG_ERROR("Invalid RDB magic string");
      return false;
    }

    uint8_t version;
    if (!Read(&version, 1)) {
      ASTRADB_LOG_ERROR("Failed to read RDB version");
      return false;
    }

    if (version != RDB_VERSION) {
      ASTRADB_LOG_ERROR("Unsupported RDB version: {}", version);
      return false;
    }

    ASTRADB_LOG_DEBUG("RDB header read, current CRC32C: {}",
                     static_cast<uint32_t>(crc_value_));
    return true;
  }

  // Read auxiliary fields and return the next opcode
  uint8_t ReadAuxFields() noexcept {
    while (true) {
      uint8_t opcode = ReadOpcode();
      if (opcode == RDB_OPCODE_EOF) {
        return opcode;
      }
      if (opcode != RDB_OPCODE_AUX) {
        // This is the first opcode of the database section, return it
        return opcode;
      }

      std::string key = ReadString();
      std::string value = ReadString();
      ASTRADB_LOG_DEBUG("RDB: Aux field {} = {}", key, value);
    }
  }

  // Read opcode
  uint8_t ReadOpcode() noexcept {
    uint8_t opcode;
    if (!Read(&opcode, 1)) {
      // Return EOF if read failed
      return RDB_OPCODE_EOF;
    }
    return opcode;
  }

  // Read length in RDB encoding format
  uint64_t ReadLength() noexcept {
    uint8_t first_byte;
    if (!Read(&first_byte, 1)) {
      return 0;
    }

    if (first_byte < 0x40) {
      return first_byte;
    } else if (first_byte < 0x80) {
      uint8_t second_byte;
      if (!Read(&second_byte, 1)) {
        return 0;
      }
      uint16_t v = (static_cast<uint16_t>(first_byte) << 8) | second_byte;
      return v & 0x3FFF;
    } else {
      uint8_t second_byte;
      if (!Read(&second_byte, 1)) {
        return 0;
      }
      if (second_byte != 0x80) {
        // 6-bit length encoding
        uint64_t len = static_cast<uint64_t>(second_byte) << 8;
        uint8_t third_byte;
        if (!Read(&third_byte, 1)) {
          return 0;
        }
        len |= third_byte;
        return len;
      } else {
        // Special encoding, not supported for now
        return 0;
      }
    }
  }

  // Read string
  std::string ReadString() noexcept {
    uint64_t len = ReadLength();
    if (len == 0) {
      return "";
    }
    std::string str(len, '\0');
    if (!Read(str.data(), len)) {
      return "";
    }
    return str;
  }

  // Read uint64
  uint64_t ReadUint64() noexcept {
    uint64_t value;
    if (!Read(&value, 8)) {
      return 0;
    }
    return value;
  }

  // Read key-value pair
  void ReadKeyValue(uint8_t type, int64_t expire_ms, int db_num,
                    const std::function<void(int, const RdbKeyValue&)>& callback) noexcept {
    std::string key = ReadString();
    if (key.empty()) {
      return;  // Failed to read key
    }
    std::string value = ReadString();
    if (value.empty()) {
      return;  // Failed to read value
    }

    RdbKeyValue kv;
    kv.type = type;
    kv.key = key;
    kv.value = value;
    kv.expire_ms = expire_ms;

    callback(db_num, kv);
  }

  // Verify checksum
  bool VerifyChecksum() noexcept {
    uint32_t file_checksum;
    current_file_->read(reinterpret_cast<char*>(&file_checksum), 4);

    uint32_t computed_checksum = static_cast<uint32_t>(crc_value_);

    ASTRADB_LOG_DEBUG("RDB checksum verification: file={}, computed={}",
                     file_checksum, computed_checksum);

    if (file_checksum != computed_checksum) {
      ASTRADB_LOG_ERROR("RDB checksum mismatch: file={}, computed={}",
                       file_checksum, computed_checksum);
      return false;
    }

    return true;
  }

  std::string file_path_;
  std::atomic<bool> initialized_{false};
  std::unique_ptr<std::ifstream> current_file_;
  bool verify_checksum_ = true;
  absl::crc32c_t crc_value_{absl::crc32c_t{0}};  // CRC32C checksum value
};

}  // namespace astra::persistence