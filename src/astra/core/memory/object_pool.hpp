// ==============================================================================
// Object Pool - Reusable object pool
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <absl/base/thread_annotations.h>
#include <absl/synchronization/mutex.h>
#include <atomic>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <vector>

#include <absl/functional/any_invocable.h>
#include "astra/base/macros.hpp"

namespace astra::core::memory {

// ==============================================================================
// ObjectPool - Thread-safe object pool
// ==============================================================================
// Features:
// - Thread-safe acquire/release
// - Optional object reuse
// - Configurable max pool size
// - Custom factory function
// ==============================================================================

template <typename T>
class ObjectPool final {
 public:
  using Factory = absl::AnyInvocable<T*()>;
  using Deleter = absl::AnyInvocable<void(T*)>;

  // Constructor
  explicit ObjectPool(
      size_t max_size = 1024,
      Factory factory = nullptr,
      Deleter deleter = nullptr);

  // Destructor
  ~ObjectPool();

  // Disable copy and move
  ASTRABI_DISABLE_COPY_MOVE(ObjectPool)

  // Acquire an object
  std::unique_ptr<T, Deleter> Acquire();

  // Release an object
  void Release(T* obj);

  // Get statistics
  size_t GetSize() const;
  size_t GetAvailable() const;
  size_t GetUsed() const;

  // Clear the pool
  void Clear();

 private:
  struct PoolDeleter {
    ObjectPool* pool;
    void operator()(T* obj) const {
      if (pool) {
        pool->Release(obj);
      }
    }
  };

  mutable absl::Mutex mutex_;
  std::deque<T*> free_objects_;
  size_t max_size_;
  size_t total_created_{0};
  Factory factory_;
  Deleter deleter_;
  std::atomic<size_t> used_count_{0};
};

// ==============================================================================
// ObjectPool Implementation
// ==============================================================================

template <typename T>
ObjectPool<T>::ObjectPool(
    size_t max_size,
    Factory factory,
    Deleter deleter)
    : max_size_(max_size),
      factory_(factory ? std::move(factory) : Factory([]() { return new T(); })),
      deleter_(deleter ? std::move(deleter) : Deleter([](T* obj) { delete obj; })) {
}

template <typename T>
ObjectPool<T>::~ObjectPool() {
  Clear();
}

template <typename T>
std::unique_ptr<T, typename ObjectPool<T>::Deleter> ObjectPool<T>::Acquire() {
  T* obj = nullptr;
  
  {
    absl::MutexLock lock(&mutex_);
    
    if (!free_objects_.empty()) {
      obj = free_objects_.front();
      free_objects_.pop_front();
    } else {
      obj = factory_();
      total_created_++;
    }
    
    used_count_.fetch_add(1, std::memory_order_relaxed);
  }
  
  return std::unique_ptr<T, Deleter>(obj, [this](T* obj) {
    // this pointer cannot be null in well-defined C++ code
    this->Release(obj);
  });
}

template <typename T>
void ObjectPool<T>::Release(T* obj) {
  if (!obj) {
    return;
  }
  
  {
    absl::MutexLock lock(&mutex_);
    
    if (free_objects_.size() < max_size_) {
      free_objects_.push_back(obj);
    } else {
      deleter_(obj);
    }
    
    used_count_.fetch_sub(1, std::memory_order_relaxed);
  }
}

template <typename T>
size_t ObjectPool<T>::GetSize() const {
  absl::MutexLock lock(&mutex_);
  return total_created_;
}

template <typename T>
size_t ObjectPool<T>::GetAvailable() const {
  absl::MutexLock lock(&mutex_);
  return free_objects_.size();
}

template <typename T>
size_t ObjectPool<T>::GetUsed() const {
  return used_count_.load(std::memory_order_relaxed);
}

template <typename T>
void ObjectPool<T>::Clear() {
  absl::MutexLock lock(&mutex_);
  
  for (auto* obj : free_objects_) {
    deleter_(obj);
  }
  
  free_objects_.clear();
}

// ==============================================================================
// SimpleObjectPool - Non-thread-safe object pool for single-threaded use
// ==============================================================================

template <typename T>
class SimpleObjectPool final {
 public:
  explicit SimpleObjectPool(size_t initial_size = 0) {
    for (size_t i = 0; i < initial_size; ++i) {
      free_objects_.push_back(new T());
    }
  }

  ~SimpleObjectPool() {
    Clear();
  }

  ASTRABI_DISABLE_COPY_MOVE(SimpleObjectPool)

  T* Acquire() {
    if (free_objects_.empty()) {
      return new T();
    }
    
    T* obj = free_objects_.back();
    free_objects_.pop_back();
    return obj;
  }

  void Release(T* obj) {
    if (obj) {
      free_objects_.push_back(obj);
    }
  }

  size_t GetAvailable() const { return free_objects_.size(); }
  size_t GetCreated() const { return created_count_; }

  void Clear() {
    for (auto* obj : free_objects_) {
      delete obj;
    }
    free_objects_.clear();
  }

 private:
  std::vector<T*> free_objects_;
  size_t created_count_{0};
};

} // namespace astra::core::memory