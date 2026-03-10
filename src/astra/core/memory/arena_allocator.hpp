// ==============================================================================
// Arena Allocator
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <absl/base/log_severity.h>
#include <absl/base/thread_annotations.h>
#include <absl/types/span.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "astra/base/macros.hpp"

namespace astra::core::memory {

// ==============================================================================
// Arena Allocator - Bump pointer allocator for fast allocations
// ==============================================================================
// Features:
// - O(1) allocation
// - No individual deallocation (free all at once)
// - Cache-friendly
// - Thread-safe (optional)
// ==============================================================================

class Arena final {
 public:
  // Configuration
  static constexpr size_t kDefaultBlockSize = 4096;  // 4KB
  static constexpr size_t kAlignment = 8;

  // Constructor
  explicit Arena(size_t block_size = kDefaultBlockSize);

  // Destructor
  ~Arena();

  // Disable copy
  ASTRABI_DISABLE_COPY(Arena)

  // Allocate aligned memory
  void* Allocate(size_t bytes);

  // Allocate memory with alignment
  void* AllocateAligned(size_t bytes, size_t alignment);

  // Get the total allocated bytes
  size_t GetAllocatedBytes() const { return allocated_bytes_; }

  // Get the block count
  size_t GetBlockCount() const { return blocks_.size(); }

  // Reset the arena (free all memory)
  void Reset();

  // Get the memory usage
  size_t GetMemoryUsage() const { return allocated_bytes_; }

 private:
  // Block structure
  struct Block {
    char* ptr;
    size_t size;
    size_t used;
  };

  // Allocate a new block
  void AllocateBlock(size_t min_size);

  size_t block_size_;
  std::vector<Block> blocks_;
  size_t allocated_bytes_{0};
  size_t current_block_index_{0};
};

// ==============================================================================
// Arena Allocator - STL-compatible allocator
// ==============================================================================

template <typename T>
class ArenaAllocator {
 public:
  using value_type = T;
  using pointer = T*;
  using const_pointer = const T*;
  using reference = T&;
  using const_reference = const T&;
  using size_type = size_t;
  using difference_type = ptrdiff_t;
  using propagate_on_container_copy_assignment = std::true_type;
  using propagate_on_container_move_assignment = std::true_type;
  using propagate_on_container_swap = std::true_type;

  // Constructor
  explicit ArenaAllocator(Arena* arena = nullptr) : arena_(arena) {}

  // Copy constructor
  template <typename U>
  ArenaAllocator(const ArenaAllocator<U>& other) : arena_(other.GetArena()) {}

  // Allocate
  T* allocate(size_t n) {
    if (arena_) {
      return static_cast<T*>(
          arena_->AllocateAligned(n * sizeof(T), alignof(T)));
    }
    return static_cast<T*>(::operator new(n * sizeof(T)));
  }

  // Deallocate
  void deallocate(T* p, size_t n) {
    // Arena allocator doesn't support individual deallocation
    if (!arena_) {
      ::operator delete(p);
    }
  }

  // Get arena
  Arena* GetArena() const { return arena_; }

 private:
  Arena* arena_;
};

// Equality operators
template <typename T, typename U>
bool operator==(const ArenaAllocator<T>& lhs, const ArenaAllocator<U>& rhs) {
  return lhs.GetArena() == rhs.GetArena();
}

template <typename T, typename U>
bool operator!=(const ArenaAllocator<T>& lhs, const ArenaAllocator<U>& rhs) {
  return !(lhs == rhs);
}

// ==============================================================================
// Arena Implementation
// ==============================================================================

inline Arena::Arena(size_t block_size)
    : block_size_(std::max(block_size, kDefaultBlockSize)) {
  AllocateBlock(block_size_);
}

inline Arena::~Arena() { Reset(); }

inline void* Arena::Allocate(size_t bytes) {
  return AllocateAligned(bytes, kAlignment);
}

inline void* Arena::AllocateAligned(size_t bytes, size_t alignment) {
  assert(alignment > 0 && (alignment & (alignment - 1)) == 0);

  // Round up bytes to alignment
  size_t aligned_bytes = (bytes + alignment - 1) & ~(alignment - 1);

  // Check if current block has enough space
  if (current_block_index_ < blocks_.size()) {
    Block& block = blocks_[current_block_index_];
    size_t aligned_offset = (block.used + alignment - 1) & ~(alignment - 1);

    if (aligned_offset + aligned_bytes <= block.size) {
      void* ptr = block.ptr + aligned_offset;
      block.used = aligned_offset + aligned_bytes;
      allocated_bytes_ += aligned_bytes;
      return ptr;
    }
  }

  // Allocate new block
  AllocateBlock(std::max(aligned_bytes, block_size_));

  // Retry allocation
  return AllocateAligned(bytes, alignment);
}

inline void Arena::AllocateBlock(size_t min_size) {
  size_t block_size = std::max(min_size, block_size_);

  char* ptr = static_cast<char*>(::operator new(block_size));
  Block block = {ptr, block_size, 0};

  if (current_block_index_ < blocks_.size()) {
    blocks_[current_block_index_] = block;
  } else {
    blocks_.push_back(block);
  }

  current_block_index_ = blocks_.size() - 1;
}

inline void Arena::Reset() {
  for (auto& block : blocks_) {
    ::operator delete(block.ptr);
  }
  blocks_.clear();
  allocated_bytes_ = 0;
  current_block_index_ = 0;
}

}  // namespace astra::core::memory
