// ==============================================================================
// AOF Writer - Append-Only File Persistence (Optimized with Abseil)
// ==============================================================================
// License: Apache 2.0
// ==============================================================================
// Design Principles:
// - noexcept for all performance-critical paths
// - Async flush with configurable fsync policy
// - Redis-compatible AOF format
// ==============================================================================

#pragma once

#include <absl/strings/string_view.h>
#include <absl/strings/str_cat.h>
#include <absl/synchronization/mutex.h>
#include <absl/synchronization/notification.h>
#include <absl/time/time.h>

#include <zstd.h>

#include <memory>
#include <string>
#include <atomic>
#include <fstream>
#include <thread>
#include <condition_variable>
#include <queue>
#include <functional>
#include <filesystem>
#include <vector>

#include <absl/functional/any_invocable.h>
#include "astra/base/macros.hpp"
#include <absl/functional/any_invocable.h>
#include "astra/base/logging.hpp"

namespace astra::persistence {

// AOF sync policies
enum class AofSyncPolicy {
  kAlways,    // Sync after every write (slowest, safest)
  kEverySec,  // Sync every second (balanced)
  kNever,     // Let OS handle it (fastest, least safe)
};

// AOF configuration options
struct AofOptions {
  std::string aof_path = "./data/aof/appendonly.aof";
  AofSyncPolicy sync_policy = AofSyncPolicy::kEverySec;
  bool auto_rewrite = true;
  size_t rewrite_min_size = 64 * 1024 * 1024;  // 64MB
  double rewrite_growth_factor = 1.0;  // Rewrite when size doubles
  size_t buffer_size = 4 * 1024 * 1024;  // 4MB write buffer
  
  // Compression options
  bool compress = false;  // Enable zstd compression
  int compression_level = 3;  // zstd compression level (1-22, default 3)
};

// AOF entry representing a write command
struct AofEntry {
  std::string command;  // Full RESP command
  
  AofEntry() = default;
  explicit AofEntry(std::string cmd) : command(std::move(cmd)) {}
};

// AOF Writer - handles append-only file writing
class AofWriter {
 public:
  AofWriter() noexcept = default;
  ~AofWriter() noexcept { Stop(); }

  // Non-copyable, non-movable
  AofWriter(const AofWriter&) = delete;
  AofWriter& operator=(const AofWriter&) = delete;
  AofWriter(AofWriter&&) = delete;
  AofWriter& operator=(AofWriter&&) = delete;

  // Initialize AOF writer
  bool Init(const AofOptions& options) noexcept {
    options_ = options;
    
    // Create directory if not exists
    std::filesystem::path p(options_.aof_path);
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    if (ec) {
      ASTRADB_LOG_ERROR("Failed to create AOF directory: {}", ec.message());
      return false;
    }
    
    // Open AOF file for appending
    file_.open(options_.aof_path, std::ios::binary | std::ios::app);
    if (ASTRADB_UNLIKELY(!file_.is_open())) {
      ASTRADB_LOG_ERROR("Failed to open AOF file: {}", options_.aof_path);
      return false;
    }
    
    // Start background sync thread if needed
    if (options_.sync_policy == AofSyncPolicy::kEverySec) {
      running_.store(true, std::memory_order_release);
      sync_thread_ = std::thread(&AofWriter::SyncThread, this);
    }
    
    initialized_.store(true, std::memory_order_release);
    ASTRADB_LOG_INFO("AOF writer initialized: {}", options_.aof_path);
    return true;
  }

  // Stop AOF writer
  void Stop() noexcept {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
      return;
    }
    
    sync_cv_.SignalAll();
    if (sync_thread_.joinable()) {
      sync_thread_.join();
    }
    
    // Flush remaining data
    Flush();
    file_.close();
    ASTRADB_LOG_INFO("AOF writer stopped");
  }

  // Append a command to AOF (RESP format)
  bool Append(absl::string_view command) noexcept {
    if (ASTRADB_UNLIKELY(!initialized_.load(std::memory_order_acquire))) {
      return false;
    }
    
    // Write to buffer
    {
      absl::MutexLock lock(&buffer_mutex_);
      buffer_.append(command.data(), command.size());
      buffer_size_.fetch_add(command.size(), std::memory_order_relaxed);
    }
    
    // Sync immediately if policy is Always
    if (options_.sync_policy == AofSyncPolicy::kAlways) {
      return Sync();
    }
    
    return true;
  }

  // Append SET command
  bool AppendSet(absl::string_view key, absl::string_view value) noexcept {
    // Format: *3\r\n$3\r\nSET\r\n$key_len\r\nkey\r\n$value_len\r\nvalue\r\n
    std::string cmd = FormatRespCommand("SET", {std::string(key), std::string(value)});
    return Append(cmd);
  }

  // Append DEL command
  bool AppendDel(absl::string_view key) noexcept {
    std::string cmd = FormatRespCommand("DEL", {std::string(key)});
    return Append(cmd);
  }

  // Append HSET command
  bool AppendHSet(absl::string_view key, absl::string_view field, absl::string_view value) noexcept {
    std::string cmd = FormatRespCommand("HSET", {std::string(key), std::string(field), std::string(value)});
    return Append(cmd);
  }

  // Append HDEL command
  bool AppendHDel(absl::string_view key, absl::string_view field) noexcept {
    std::string cmd = FormatRespCommand("HDEL", {std::string(key), std::string(field)});
    return Append(cmd);
  }

  // Append SADD command
  bool AppendSAdd(absl::string_view key, absl::string_view member) noexcept {
    std::string cmd = FormatRespCommand("SADD", {std::string(key), std::string(member)});
    return Append(cmd);
  }

  // Append SREM command
  bool AppendSRem(absl::string_view key, absl::string_view member) noexcept {
    std::string cmd = FormatRespCommand("SREM", {std::string(key), std::string(member)});
    return Append(cmd);
  }

  // Append ZADD command
  bool AppendZAdd(absl::string_view key, double score, absl::string_view member) noexcept {
    std::string cmd = FormatRespCommand("ZADD", {std::string(key), absl::StrCat(score), std::string(member)});
    return Append(cmd);
  }

  // Append ZREM command
  bool AppendZRem(absl::string_view key, absl::string_view member) noexcept {
    std::string cmd = FormatRespCommand("ZREM", {std::string(key), std::string(member)});
    return Append(cmd);
  }

  // Append LPUSH command
  bool AppendLPush(absl::string_view key, absl::string_view value) noexcept {
    std::string cmd = FormatRespCommand("LPUSH", {std::string(key), std::string(value)});
    return Append(cmd);
  }

  // Append RPUSH command
  bool AppendRPush(absl::string_view key, absl::string_view value) noexcept {
    std::string cmd = FormatRespCommand("RPUSH", {std::string(key), std::string(value)});
    return Append(cmd);
  }

  // Append LPOP command
  bool AppendLPop(absl::string_view key) noexcept {
    std::string cmd = FormatRespCommand("LPOP", {std::string(key)});
    return Append(cmd);
  }

  // Append RPOP command
  bool AppendRPop(absl::string_view key) noexcept {
    std::string cmd = FormatRespCommand("RPOP", {std::string(key)});
    return Append(cmd);
  }

  // Append EXPIRE command
  bool AppendExpire(absl::string_view key, int64_t seconds) noexcept {
    std::string cmd = FormatRespCommand("EXPIRE", {std::string(key), absl::StrCat(seconds)});
    return Append(cmd);
  }

  // Append PEXPIRE command
  bool AppendPExpire(absl::string_view key, int64_t ms) noexcept {
    std::string cmd = FormatRespCommand("PEXPIRE", {std::string(key), absl::StrCat(ms)});
    return Append(cmd);
  }

  // Flush buffer to disk
  bool Flush() noexcept {
    absl::MutexLock lock(&buffer_mutex_);
    if (buffer_.empty()) {
      return true;
    }
    
    if (options_.compress) {
      // Compress with zstd
      return FlushCompressed();
    } else {
      // Write uncompressed
      file_.write(buffer_.data(), buffer_.size());
      file_.flush();
      buffer_.clear();
      buffer_size_.store(0, std::memory_order_relaxed);
      return file_.good();
    }
  }
  
  // Flush with zstd compression
  bool FlushCompressed() noexcept {
    // Reserve space for compressed data
    size_t bound = ZSTD_compressBound(buffer_.size());
    compress_buffer_.resize(bound);
    
    // Compress
    size_t compressed_size = ZSTD_compress(
        compress_buffer_.data(), compress_buffer_.size(),
        buffer_.data(), buffer_.size(),
        options_.compression_level);
    
    if (ZSTD_isError(compressed_size)) {
      ASTRADB_LOG_ERROR("zstd compression failed: {}", ZSTD_getErrorName(compressed_size));
      // Fallback to uncompressed
      file_.write(buffer_.data(), buffer_.size());
    } else {
      // Write compressed block: [size(4 bytes)][compressed data]
      uint32_t size = static_cast<uint32_t>(compressed_size);
      file_.write(reinterpret_cast<const char*>(&size), sizeof(size));
      file_.write(reinterpret_cast<const char*>(compress_buffer_.data()), compressed_size);
    }
    
    file_.flush();
    buffer_.clear();
    buffer_size_.store(0, std::memory_order_relaxed);
    return file_.good();
  }

  // Sync file to disk
  bool Sync() noexcept {
    if (!Flush()) {
      return false;
    }
    file_.sync_with_stdio();
    return true;
  }

  // Get current AOF file size
  size_t GetFileSize() const noexcept {
    if (!const_cast<AofWriter*>(this)->file_.is_open()) {
      return 0;
    }
    auto pos = const_cast<AofWriter*>(this)->file_.tellp();
    return pos >= 0 ? static_cast<size_t>(pos) : 0;
  }

  // Get buffer size
  size_t GetBufferSize() const noexcept {
    return buffer_size_.load(std::memory_order_relaxed);
  }

  // Check if initialized
  bool IsInitialized() const noexcept {
    return initialized_.load(std::memory_order_acquire);
  }

  // Rewrite AOF - creates new AOF from current database state
  bool Rewrite(absl::AnyInvocable<void(std::string&)> write_callback) noexcept {
    if (!initialized_.load(std::memory_order_acquire)) {
      return false;
    }

    // Stop background sync during rewrite
    std::atomic<bool> was_running = running_.exchange(false, std::memory_order_acq_rel);
    sync_cv_.SignalAll();
    if (sync_thread_.joinable()) {
      sync_thread_.join();
    }

    // Flush any pending data first
    Flush();

    // Create temporary file for rewrite
    std::string temp_path = options_.aof_path + ".tmp-rewrite";
    std::ofstream temp_file(temp_path, std::ios::binary);
    if (!temp_file.is_open()) {
      ASTRADB_LOG_ERROR("Failed to create temporary AOF file: {}", temp_path);
      if (was_running) {
        // Restart sync thread
        running_.store(true, std::memory_order_release);
        sync_thread_ = std::thread(&AofWriter::SyncThread, this);
      }
      return false;
    }

    // Call callback to write all data
    std::string data;
    write_callback(data);
    temp_file.write(data.data(), data.size());
    temp_file.flush();
    temp_file.close();

    // Replace old AOF with new one
    std::filesystem::path temp_p(temp_path);
    std::filesystem::path final_p(options_.aof_path);
    std::error_code ec;
    
    // Backup old AOF
    std::string backup_path = options_.aof_path + ".backup";
    if (std::filesystem::exists(final_p, ec)) {
      std::filesystem::rename(final_p, backup_path, ec);
      if (ec) {
        ASTRADB_LOG_WARN("Failed to backup old AOF: {}", ec.message());
      }
    }
    
    // Rename temp file to final file
    std::filesystem::rename(temp_p, final_p, ec);
    if (ec) {
      ASTRADB_LOG_ERROR("Failed to rename rewritten AOF: {}", ec.message());
      // Restore from backup
      if (std::filesystem::exists(backup_path, ec)) {
        std::filesystem::rename(backup_path, final_p, ec);
      }
      return false;
    }

    // Remove backup
    if (std::filesystem::exists(backup_path, ec)) {
      std::filesystem::remove(backup_path, ec);
    }

    // Reopen AOF file
    file_.close();
    file_.open(options_.aof_path, std::ios::binary | std::ios::app);
    if (!file_.is_open()) {
      ASTRADB_LOG_ERROR("Failed to reopen AOF after rewrite");
      return false;
    }

    ASTRADB_LOG_INFO("AOF rewrite completed: {} bytes", data.size());

    // Restart sync thread if it was running
    if (was_running && options_.sync_policy == AofSyncPolicy::kEverySec) {
      running_.store(true, std::memory_order_release);
      sync_thread_ = std::thread(&AofWriter::SyncThread, this);
    }

    return true;
  }

  // Check if rewrite is needed
  bool ShouldRewrite() const noexcept {
    size_t current_size = GetFileSize();
    return current_size > options_.rewrite_min_size && 
           current_size > options_.rewrite_min_size * options_.rewrite_growth_factor;
  }

 private:
  // Format a command in RESP format
  static std::string FormatRespCommand(absl::string_view cmd_name,
                                       const std::vector<std::string>& args) {
    std::string result;
    const size_t total_args = args.size() + 1;  // +1 for command name
    
    // Calculate total size for efficiency
    size_t total_size = 32;  // Overhead for array header
    total_size += cmd_name.size() + 16;
    for (const auto& arg : args) {
      total_size += arg.size() + 16;
    }
    result.reserve(total_size);
    
    // Array header: *N\r\n
    absl::StrAppend(&result, "*", total_args, "\r\n");
    
    // Command name: $len\r\ncmd\r\n
    absl::StrAppend(&result, "$", cmd_name.size(), "\r\n", cmd_name, "\r\n");
    
    // Arguments
    for (const auto& arg : args) {
      absl::StrAppend(&result, "$", arg.size(), "\r\n", arg, "\r\n");
    }
    
    return result;
  }

  // Background sync thread
  void SyncThread() noexcept {
    while (running_.load(std::memory_order_acquire)) {
      absl::MutexLock lock(&sync_mutex_);
      sync_cv_.WaitWithTimeout(&sync_mutex_, absl::Seconds(1));
      
      if (!running_.load(std::memory_order_acquire)) {
        break;
      }
      
      Sync();
    }
  }

  AofOptions options_;
  std::ofstream file_;
  std::string buffer_;
  std::vector<char> compress_buffer_;  // For zstd compression
  absl::Mutex buffer_mutex_;
  std::atomic<size_t> buffer_size_{0};
  std::atomic<bool> initialized_{false};
  std::atomic<bool> running_{false};
  
  std::thread sync_thread_;
  absl::Mutex sync_mutex_;
  absl::CondVar sync_cv_;
};

}  // namespace astra::persistence
