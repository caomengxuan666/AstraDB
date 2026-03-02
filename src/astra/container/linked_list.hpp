// ==============================================================================
// Optimized List - Redis-compatible list implementation
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <cstdint>
#include <deque>
#include <vector>
#include <string>
#include <memory>

namespace astra::container {

// Threshold for switching from vector to deque
constexpr size_t kListVectorThreshold = 256;

// Optimized list implementation
// Uses std::vector for small lists (better cache locality)
// Uses std::deque for large lists (O(1) push/pop at both ends)
template <typename T>
class LinkedList {
 public:
  using value_type = T;
  using iterator = typename std::deque<T>::iterator;
  using const_iterator = typename std::deque<T>::const_iterator;

  LinkedList() = default;
  ~LinkedList() = default;

  // Disable copy
  LinkedList(const LinkedList&) = delete;
  LinkedList& operator=(const LinkedList&) = delete;

  // Enable move
  LinkedList(LinkedList&&) = default;
  LinkedList& operator=(LinkedList&&) = default;

  // ========== Push Operations ==========

  // Push to left (head)
  void PushLeft(const T& value) {
    if (use_vector_) {
      vector_.insert(vector_.begin(), value);
      CheckConvertToDeque();
    } else {
      deque_.push_front(value);
    }
  }

  void PushLeft(T&& value) {
    if (use_vector_) {
      vector_.insert(vector_.begin(), std::move(value));
      CheckConvertToDeque();
    } else {
      deque_.push_front(std::move(value));
    }
  }

  // Push to right (tail)
  void PushRight(const T& value) {
    if (use_vector_) {
      vector_.push_back(value);
      CheckConvertToDeque();
    } else {
      deque_.push_back(value);
    }
  }

  void PushRight(T&& value) {
    if (use_vector_) {
      vector_.push_back(std::move(value));
      CheckConvertToDeque();
    } else {
      deque_.push_back(std::move(value));
    }
  }

  // ========== Pop Operations ==========

  // Pop from left (head)
  std::optional<T> PopLeft() {
    if (use_vector_) {
      if (vector_.empty()) {
        return std::nullopt;
      }
      auto value = std::move(vector_.front());
      vector_.erase(vector_.begin());
      return value;
    } else {
      if (deque_.empty()) {
        return std::nullopt;
      }
      auto value = std::move(deque_.front());
      deque_.pop_front();
      return value;
    }
  }

  // Pop from right (tail)
  std::optional<T> PopRight() {
    if (use_vector_) {
      if (vector_.empty()) {
        return std::nullopt;
      }
      auto value = std::move(vector_.back());
      vector_.pop_back();
      return value;
    } else {
      if (deque_.empty()) {
        return std::nullopt;
      }
      auto value = std::move(deque_.back());
      deque_.pop_back();
      return value;
    }
  }

  // ========== Index Operations ==========

  // Get element at index (supports negative indices)
  std::optional<T> Index(int64_t index) const {
    int64_t size = Size();
    if (size == 0) {
      return std::nullopt;
    }

    // Handle negative indices
    if (index < 0) {
      index = size + index;
      if (index < 0) {
        index = 0;
      }
    }

    if (index >= size) {
      return std::nullopt;
    }

    if (use_vector_) {
      return vector_[index];
    } else {
      return deque_[index];
    }
  }

  // Set element at index (supports negative indices)
  bool Set(int64_t index, const T& value) {
    int64_t size = Size();
    if (size == 0) {
      return false;
    }

    // Handle negative indices
    if (index < 0) {
      index = size + index;
      if (index < 0) {
        return false;
      }
    }

    if (index >= size) {
      return false;
    }

    if (use_vector_) {
      vector_[index] = value;
    } else {
      deque_[index] = value;
    }
    return true;
  }

  bool Set(int64_t index, T&& value) {
    int64_t size = Size();
    if (size == 0) {
      return false;
    }

    // Handle negative indices
    if (index < 0) {
      index = size + index;
      if (index < 0) {
        return false;
      }
    }

    if (index >= size) {
      return false;
    }

    if (use_vector_) {
      vector_[index] = std::move(value);
    } else {
      deque_[index] = std::move(value);
    }
    return true;
  }

  // ========== Range Operations ==========

  // Get range of elements [start, stop]
  std::vector<T> Range(int64_t start, int64_t stop) const {
    std::vector<T> result;
    int64_t size = Size();
    
    if (size == 0) {
      return result;
    }

    // Handle negative indices
    if (start < 0) {
      start = size + start;
      if (start < 0) {
        start = 0;
      }
    }

    if (stop < 0) {
      stop = size + stop;
      if (stop < 0) {
        stop = -1;
      }
    }

    // Clamp to valid range
    if (start >= size) {
      return result;
    }
    if (stop >= size) {
      stop = size - 1;
    }
    if (start > stop) {
      return result;
    }

    // Reserve space
    result.reserve(stop - start + 1);

    // Copy elements
    if (use_vector_) {
      result.insert(result.end(),
                    vector_.begin() + start,
                    vector_.begin() + stop + 1);
    } else {
      result.insert(result.end(),
                    deque_.begin() + start,
                    deque_.begin() + stop + 1);
    }

    return result;
  }

  // Trim list to keep only elements in [start, stop]
  void Trim(int64_t start, int64_t stop) {
    int64_t size = Size();
    
    if (size == 0) {
      return;
    }

    // Handle negative indices
    if (start < 0) {
      start = size + start;
      if (start < 0) {
        start = 0;
      }
    }

    if (stop < 0) {
      stop = size + stop;
      if (stop < 0) {
        stop = -1;
      }
    }

    // Clamp to valid range
    if (start >= size) {
      Clear();
      return;
    }
    if (stop >= size) {
      stop = size - 1;
    }
    if (start > stop) {
      Clear();
      return;
    }

    // Trim the list
    if (use_vector_) {
      vector_.erase(vector_.begin() + stop + 1, vector_.end());
      vector_.erase(vector_.begin(), vector_.begin() + start);
    } else {
      deque_.erase(deque_.begin() + stop + 1, deque_.end());
      deque_.erase(deque_.begin(), deque_.begin() + start);
    }
  }

  // ========== Remove Operations ==========

  // Remove first count occurrences of value
  size_t Remove(const T& value, int64_t count = 0) {
    size_t removed = 0;
    
    if (use_vector_) {
      auto it = vector_.begin();
      while (it != vector_.end()) {
        if (*it == value) {
          it = vector_.erase(it);
          ++removed;
          if (count > 0 && static_cast<int64_t>(removed) >= count) {
            break;
          }
        } else {
          ++it;
        }
      }
    } else {
      auto it = deque_.begin();
      while (it != deque_.end()) {
        if (*it == value) {
          it = deque_.erase(it);
          ++removed;
          if (count > 0 && static_cast<int64_t>(removed) >= count) {
            break;
          }
        } else {
          ++it;
        }
      }
    }
    
    return removed;
  }

  // ========== Insert Operations ==========

  // Insert value before/after pivot
  bool Insert(int64_t pivot_index, const T& value, bool before = true) {
    int64_t size = Size();
    
    if (size == 0) {
      return false;
    }

    // Handle negative indices
    if (pivot_index < 0) {
      pivot_index = size + pivot_index;
      if (pivot_index < 0) {
        return false;
      }
    }

    if (pivot_index >= size) {
      return false;
    }

    // Insert at correct position
    if (use_vector_) {
      if (before) {
        vector_.insert(vector_.begin() + pivot_index, value);
      } else {
        vector_.insert(vector_.begin() + pivot_index + 1, value);
      }
      CheckConvertToDeque();
    } else {
      if (before) {
        deque_.insert(deque_.begin() + pivot_index, value);
      } else {
        deque_.insert(deque_.begin() + pivot_index + 1, value);
      }
    }
    
    return true;
  }

  // ========== Utility Operations ==========

  size_t Size() const {
    if (use_vector_) {
      return vector_.size();
    } else {
      return deque_.size();
    }
  }

  bool Empty() const {
    if (use_vector_) {
      return vector_.empty();
    } else {
      return deque_.empty();
    }
  }

  void Clear() {
    if (use_vector_) {
      vector_.clear();
    } else {
      deque_.clear();
    }
  }

  // ========== Memory Operations ==========

  size_t MemoryUsage() const {
    if (use_vector_) {
      return vector_.capacity() * sizeof(T);
    } else {
      return deque_.size() * sizeof(T) + deque_.size() * sizeof(void*);
    }
  }

 private:
  // Check if we should convert from vector to deque
  void CheckConvertToDeque() {
    if (use_vector_ && vector_.size() > kListVectorThreshold) {
      // Convert to deque
      deque_.insert(deque_.end(), vector_.begin(), vector_.end());
      vector_.clear();
      use_vector_ = false;
    }
  }

  bool use_vector_ = true;
  std::vector<T> vector_;
  std::deque<T> deque_;
};

// Specialization for std::string (common use case)
using StringList = LinkedList<std::string>;

}  // namespace astra::container