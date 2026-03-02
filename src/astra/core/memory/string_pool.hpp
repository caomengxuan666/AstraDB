// ==============================================================================
// String Pool - Efficient string memory management
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <absl/base/log_severity.h>
#include <absl/base/thread_annotations.h>
#include <absl/container/fixed_array.h>
#include <absl/synchronization/mutex.h>
#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>
#include <string>

#if defined(_WIN32) || defined(_WIN64)
  #include <malloc.h>
#elif defined(__ANDROID__)
  #include <cstdlib>
#endif

#include "astra/base/macros.hpp"

namespace astra::core::memory {

// ==============================================================================
// String Block - A block of pre-allocated string memory
// ==============================================================================

class StringBlock final {
 public:
  static constexpr size_t kDefaultBlockSize = 64 * 1024;  // 64KB per block
  static constexpr size_t kAlignment = 8;

  explicit StringBlock(size_t size = kDefaultBlockSize)
      : size_(size),
        offset_(0),
        data_(AllocateAligned(size)) {
    if (!data_) {
      throw std::bad_alloc();
    }
  }

  ~StringBlock() {
    FreeAligned(data_);
  }

  ASTRABI_DISABLE_COPY_MOVE(StringBlock)

  // Allocate space for a string
  char* Allocate(size_t len) {
    if (offset_ + len > size_) {
      return nullptr;
    }
    char* ptr = data_ + offset_;
    offset_ += len;
    return ptr;
  }

  // Check if this block has enough space
  bool HasSpace(size_t len) const {
    return offset_ + len <= size_;
  }

  // Get remaining space
  size_t Remaining() const {
    return size_ - offset_;
  }

  // Reset the block (reuse for new allocations)
  void Reset() {
    offset_ = 0;
  }

  size_t GetOffset() const {
    return offset_;
  }

 private:
  static char* AllocateAligned(size_t size) {
#if defined(_WIN32) || defined(_WIN64)
    // Windows: Use _aligned_malloc
    return static_cast<char*>(_aligned_malloc(size, kAlignment));
#elif defined(__ANDROID__)
    // Android: Use posix_memalign
    void* ptr = nullptr;
    if (posix_memalign(&ptr, kAlignment, size) != 0) {
      return nullptr;
    }
    return static_cast<char*>(ptr);
#else
    // POSIX: Use aligned_alloc (C++17) or fallback to aligned_alloc
    return static_cast<char*>(std::aligned_alloc(kAlignment, size));
#endif
  }

  static void FreeAligned(char* ptr) {
    if (ptr) {
#if defined(_WIN32) || defined(_WIN64)
      _aligned_free(ptr);
#else
      std::free(ptr);
#endif
    }
  }

  size_t size_;
  size_t offset_;
  char* data_;
};

// ==============================================================================
// StringPool - Pool allocator for strings
// ==============================================================================

class StringPool final {
 public:
  explicit StringPool(size_t block_size = StringBlock::kDefaultBlockSize)
      : block_size_(block_size) {
    // Pre-allocate one block
    AddBlock();
  }

  ~StringPool() {
    absl::MutexLock lock(&mutex_);
    for (auto* block : blocks_) {
      delete block;
    }
  }

  ASTRABI_DISABLE_COPY_MOVE(StringPool)

  // Allocate a string with automatic null-termination
  std::string_view AllocateString(const char* data, size_t len) {
    if (len == 0) {
      return "";
    }

    char* ptr = Allocate(len + 1);
    if (!ptr) {
      throw std::bad_alloc();
    }

    std::memcpy(ptr, data, len);
    ptr[len] = '\0';

    return std::string_view(ptr, len);
  }

  // Allocate raw buffer
  char* Allocate(size_t size) {
    absl::MutexLock lock(&mutex_);
    
    // Try to allocate from current block
    char* ptr = current_block_->Allocate(size);
    if (ptr) {
      allocated_bytes_.fetch_add(size, std::memory_order_relaxed);
      return ptr;
    }

    // Current block is full, try to find space in existing blocks
    for (auto* block : blocks_) {
      if (block != current_block_ && block->HasSpace(size)) {
        ptr = block->Allocate(size);
        if (ptr) {
          current_block_ = block;
          allocated_bytes_.fetch_add(size, std::memory_order_relaxed);
          return ptr;
        }
      }
    }

    // No space in existing blocks, add a new block
    size_t new_block_size = std::max(block_size_, size);
    AddBlock(new_block_size);

    ptr = current_block_->Allocate(size);
    if (!ptr) {
      throw std::bad_alloc();
    }

    allocated_bytes_.fetch_add(size, std::memory_order_relaxed);
    return ptr;
  }

  // Get statistics
  size_t GetTotalBlocks() const {
    absl::MutexLock lock(&mutex_);
    return blocks_.size();
  }

  size_t GetAllocatedBytes() const {
    return allocated_bytes_.load(std::memory_order_relaxed);
  }

  size_t GetTotalCapacity() const {
    absl::MutexLock lock(&mutex_);
    size_t total = 0;
    for (const auto* block : blocks_) {
      total += block->GetOffset();
    }
    return total;
  }

  // Reset all blocks (clear all allocations)
  void Reset() {
    absl::MutexLock lock(&mutex_);
    for (auto* block : blocks_) {
      block->Reset();
    }
    current_block_ = blocks_[0];
    allocated_bytes_.store(0, std::memory_order_relaxed);
  }

  // Compact the pool by removing unused blocks
  void Compact() {
    absl::MutexLock lock(&mutex_);
    
    // Keep at least one block
    auto it = blocks_.begin();
    ++it;
    
    while (it != blocks_.end()) {
      if ((*it)->GetOffset() == 0) {
        delete *it;
        it = blocks_.erase(it);
      } else {
        ++it;
      }
    }
  }

 private:
  void AddBlock(size_t size = 0) {
    if (size == 0) {
      size = block_size_;
    }
    auto* block = new StringBlock(size);
    blocks_.push_back(block);
    current_block_ = block;
  }

  mutable absl::Mutex mutex_;
  size_t block_size_;
  std::vector<StringBlock*> blocks_ ABSL_GUARDED_BY(mutex_);
  StringBlock* current_block_ ABSL_GUARDED_BY(mutex_);
  std::atomic<size_t> allocated_bytes_{0};
};

// ==============================================================================
// SmallStringOpt - Small string optimization for short strings
// ==============================================================================

template <size_t InlineSize = 15>
class SmallStringOpt {
 public:
  SmallStringOpt() = default;
  
  explicit SmallStringOpt(const std::string& str) {
    Assign(str.data(), str.size());
  }
  
  explicit SmallStringOpt(std::string_view str) {
    Assign(str.data(), str.size());
  }
  
  SmallStringOpt(const char* data, size_t len) {
    Assign(data, len);
  }

  void Assign(const char* data, size_t len) {
    if (len <= InlineSize) {
      // Store inline
      size_ = len;
      std::memcpy(inline_data_, data, len);
      inline_data_[len] = '\0';
      heap_data_ = nullptr;
    } else {
      // Store on heap
      heap_data_ = static_cast<char*>(std::malloc(len + 1));
      if (!heap_data_) {
        throw std::bad_alloc();
      }
      std::memcpy(heap_data_, data, len);
      heap_data_[len] = '\0';
      size_ = len;
    }
  }

  ~SmallStringOpt() {
    if (!IsInline()) {
      std::free(heap_data_);
    }
  }

  ASTRABI_DISABLE_COPY(SmallStringOpt)

  SmallStringOpt(SmallStringOpt&& other) noexcept {
    MoveFrom(std::move(other));
  }

  SmallStringOpt& operator=(SmallStringOpt&& other) noexcept {
    if (this != &other) {
      Clear();
      MoveFrom(std::move(other));
    }
    return *this;
  }

  // Get string view
  std::string_view View() const {
    if (IsInline()) {
      return std::string_view(inline_data_, size_);
    }
    return std::string_view(heap_data_, size_);
  }

  // Get C string
  const char* CStr() const {
    if (IsInline()) {
      return inline_data_;
    }
    return heap_data_;
  }

  // Get size
  size_t Size() const {
    return size_;
  }

  // Is empty
  bool Empty() const {
    return size_ == 0;
  }

  // Clear
  void Clear() {
    if (!IsInline()) {
      std::free(heap_data_);
      heap_data_ = nullptr;
    }
    size_ = 0;
  }

 private:
  bool IsInline() const {
    return size_ <= InlineSize;
  }

  void MoveFrom(SmallStringOpt&& other) {
    size_ = other.size_;
    if (other.IsInline()) {
      std::memcpy(inline_data_, other.inline_data_, InlineSize + 1);
    } else {
      heap_data_ = other.heap_data_;
      other.heap_data_ = nullptr;
    }
    other.size_ = 0;
  }

  char inline_data_[InlineSize + 1];
  char* heap_data_;
  size_t size_;
};

}  // namespace astra::core::memory