// ==============================================================================
// Buffer Pool - Zero-copy buffer management
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

#include "astra/base/macros.hpp"

namespace astra::core::memory {

// ==============================================================================
// Buffer - Reference-counted buffer
// ==============================================================================

class Buffer final {
 public:
  // Buffer size classes
  static constexpr size_t kSmallBufferSize = 64;      // 64 bytes
  static constexpr size_t kMediumBufferSize = 4096;    // 4KB
  static constexpr size_t kLargeBufferSize = 65536;    // 64KB
  static constexpr size_t kXLargeBufferSize = 1048576; // 1MB

  // Constructor
  explicit Buffer(size_t size);
  Buffer(const void* data, size_t size);
  Buffer(size_t size, std::unique_ptr<char[]> data);

  // Destructor
  ~Buffer();

  // Disable copy
  ASTRABI_DISABLE_COPY(Buffer)

  // Move
  Buffer(Buffer&& other) noexcept;
  Buffer& operator=(Buffer&& other) noexcept;

  // Get data pointer
  char* data() { return data_.get(); }
  const char* data() const { return data_.get(); }

  // Get size
  size_t size() const { return size_; }

  // Get capacity
  size_t capacity() const { return capacity_; }

  // Resize
  void Resize(size_t new_size);

  // Clear
  void Clear() { size_ = 0; }

  // Is empty
  bool empty() const { return size_ == 0; }

  // Append data
  void Append(const void* data, size_t len);

  // Reserve capacity
  void Reserve(size_t new_capacity);

  // Get reference count
  int ref_count() const { return ref_count_.load(); }

  // Add reference
  void AddRef();

  // Release reference
  void Release();

 private:
  std::unique_ptr<char[]> data_;
  size_t size_{0};
  size_t capacity_{0};
  std::atomic<int> ref_count_{0};
};

// ==============================================================================
// BufferPtr - Smart pointer to Buffer
// ==============================================================================

class BufferPtr final {
 public:
  BufferPtr() = default;
  explicit BufferPtr(Buffer* buffer);
  ~BufferPtr();

  // Disable copy
  ASTRABI_DISABLE_COPY(BufferPtr)

  // Move
  BufferPtr(BufferPtr&& other) noexcept;
  BufferPtr& operator=(BufferPtr&& other) noexcept;

  // Get buffer
  Buffer* get() const { return buffer_; }

  // Release ownership
  Buffer* release();

  // Reset
  void Reset(Buffer* buffer = nullptr);

  // Operator overloads
  Buffer& operator*() const { return *buffer_; }
  Buffer* operator->() const { return buffer_; }
  explicit operator bool() const { return buffer_ != nullptr; }

 private:
  Buffer* buffer_{nullptr};
};

// ==============================================================================
// BufferPool - Pool of pre-allocated buffers
// ==============================================================================

class BufferPool final {
 public:
  // Constructor
  explicit BufferPool(size_t max_buffer_size = Buffer::kXLargeBufferSize);
  
  // Destructor
  ~BufferPool();

  // Disable copy and move
  ASTRABI_DISABLE_COPY_MOVE(BufferPool)

  // Acquire a buffer
  BufferPtr Acquire(size_t size);

  // Return a buffer to the pool
  void Release(Buffer* buffer);

  // Get statistics
  size_t GetTotalBuffers() const;
  size_t GetAvailableBuffers() const;
  size_t GetUsedBuffers() const;

 private:
  // Buffer bucket
  struct BufferBucket {
    std::vector<Buffer*> free_buffers;
    std::atomic<size_t> total_count{0};
    std::atomic<size_t> used_count{0};
  };

  // Get bucket index for size
  size_t GetBucketIndex(size_t size) const;

  // Allocate a new buffer
  Buffer* AllocateBuffer(size_t size);

  mutable absl::Mutex mutex_;
  static constexpr size_t kNumBuckets = 4;
  std::unique_ptr<BufferBucket[]> buckets_;
  [[maybe_unused]] size_t max_buffer_size_;  // Reserved for future size limit enforcement
};

// ==============================================================================
// Buffer Implementation
// ==============================================================================

inline Buffer::Buffer(size_t size)
    : data_(std::make_unique<char[]>(size)),
      size_(0),
      capacity_(size) {
}

inline Buffer::Buffer(const void* data, size_t size)
    : data_(std::make_unique<char[]>(size)),
      size_(size),
      capacity_(size) {
  std::memcpy(data_.get(), data, size);
}

inline Buffer::Buffer(size_t size, std::unique_ptr<char[]> data)
    : data_(std::move(data)),
      size_(size),
      capacity_(size) {
}

inline Buffer::~Buffer() = default;

inline Buffer::Buffer(Buffer&& other) noexcept
    : data_(std::move(other.data_)),
      size_(other.size_),
      capacity_(other.capacity_),
      ref_count_(other.ref_count_.load()) {
  other.size_ = 0;
  other.capacity_ = 0;
}

inline Buffer& Buffer::operator=(Buffer&& other) noexcept {
  if (this != &other) {
    data_ = std::move(other.data_);
    size_ = other.size_;
    capacity_ = other.capacity_;
    ref_count_ = other.ref_count_.load();
    other.size_ = 0;
    other.capacity_ = 0;
  }
  return *this;
}

inline void Buffer::Resize(size_t new_size) {
  if (new_size > capacity_) {
    Reserve(new_size);
  }
  size_ = new_size;
}

inline void Buffer::Append(const void* data, size_t len) {
  if (size_ + len > capacity_) {
    Reserve(std::max(size_ * 2, size_ + len));
  }
  std::memcpy(data_.get() + size_, data, len);
  size_ += len;
}

inline void Buffer::Reserve(size_t new_capacity) {
  if (new_capacity <= capacity_) {
    return;
  }
  
  auto new_data = std::make_unique<char[]>(new_capacity);
  std::memcpy(new_data.get(), data_.get(), size_);
  data_ = std::move(new_data);
  capacity_ = new_capacity;
}

inline void Buffer::AddRef() {
  ref_count_.fetch_add(1, std::memory_order_relaxed);
}

inline void Buffer::Release() {
  if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    delete this;
  }
}

// ==============================================================================
// BufferPtr Implementation
// ==============================================================================

inline BufferPtr::BufferPtr(Buffer* buffer) : buffer_(buffer) {
  if (buffer_) {
    buffer_->AddRef();
  }
}

inline BufferPtr::~BufferPtr() {
  Reset();
}

inline BufferPtr::BufferPtr(BufferPtr&& other) noexcept
    : buffer_(other.buffer_) {
  other.buffer_ = nullptr;
}

inline BufferPtr& BufferPtr::operator=(BufferPtr&& other) noexcept {
  if (this != &other) {
    Reset();
    buffer_ = other.buffer_;
    other.buffer_ = nullptr;
  }
  return *this;
}

inline Buffer* BufferPtr::release() {
  Buffer* tmp = buffer_;
  buffer_ = nullptr;
  return tmp;
}

inline void BufferPtr::Reset(Buffer* buffer) {
  if (buffer_ != buffer) {
    if (buffer_) {
      buffer_->Release();
    }
    buffer_ = buffer;
    if (buffer_) {
      buffer_->AddRef();
    }
  }
}

// ==============================================================================
// BufferPool Implementation
// ==============================================================================

inline BufferPool::BufferPool(size_t max_buffer_size)
    : max_buffer_size_(max_buffer_size) {
  // Create buckets for different buffer sizes
  buckets_ = std::make_unique<BufferBucket[]>(kNumBuckets);
}

inline BufferPool::~BufferPool() {
  absl::MutexLock lock(&mutex_);
  for (size_t i = 0; i < kNumBuckets; ++i) {
    for (auto* buffer : buckets_[i].free_buffers) {
      delete buffer;
    }
    buckets_[i].free_buffers.clear();
  }
}

inline BufferPtr BufferPool::Acquire(size_t size) {
  size_t bucket_index = GetBucketIndex(size);

  {
    absl::MutexLock lock(&mutex_);
    assert(bucket_index < kNumBuckets);

    auto& bucket = buckets_[bucket_index];

    if (!bucket.free_buffers.empty()) {
      Buffer* buffer = bucket.free_buffers.back();
      bucket.free_buffers.pop_back();
      buffer->Resize(size);
      bucket.used_count.fetch_add(1, std::memory_order_relaxed);
      return BufferPtr(buffer);
    }
  }

  // Allocate new buffer
  Buffer* buffer = AllocateBuffer(size);
  {
    absl::MutexLock lock(&mutex_);
    buckets_[bucket_index].total_count.fetch_add(1, std::memory_order_relaxed);
    buckets_[bucket_index].used_count.fetch_add(1, std::memory_order_relaxed);
  }
  return BufferPtr(buffer);
}

inline void BufferPool::Release(Buffer* buffer) {
  if (!buffer) {
    return;
  }

  size_t bucket_index = GetBucketIndex(buffer->capacity());

  absl::MutexLock lock(&mutex_);
  assert(bucket_index < kNumBuckets);

  auto& bucket = buckets_[bucket_index];
  bucket.free_buffers.push_back(buffer);
  bucket.used_count.fetch_sub(1, std::memory_order_relaxed);
}

inline size_t BufferPool::GetBucketIndex(size_t size) const {
  if (size <= Buffer::kSmallBufferSize) {
    return 0;
  } else if (size <= Buffer::kMediumBufferSize) {
    return 1;
  } else if (size <= Buffer::kLargeBufferSize) {
    return 2;
  } else {
    return 3;
  }
}

inline Buffer* BufferPool::AllocateBuffer(size_t size) {
  return new Buffer(size);
}

inline size_t BufferPool::GetTotalBuffers() const {
  size_t total = 0;
  absl::MutexLock lock(&mutex_);
  for (size_t i = 0; i < kNumBuckets; ++i) {
    total += buckets_[i].total_count.load(std::memory_order_relaxed);
  }
  return total;
}

inline size_t BufferPool::GetAvailableBuffers() const {
  size_t available = 0;
  absl::MutexLock lock(&mutex_);
  for (size_t i = 0; i < kNumBuckets; ++i) {
    available += buckets_[i].free_buffers.size();
  }
  return available;
}

inline size_t BufferPool::GetUsedBuffers() const {
  size_t used = 0;
  absl::MutexLock lock(&mutex_);
  for (size_t i = 0; i < kNumBuckets; ++i) {
    used += buckets_[i].used_count.load(std::memory_order_relaxed);
  }
  return used;
}

} // namespace astra::core::memory