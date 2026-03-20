// ==============================================================================
// Object Size Estimator - Estimate memory usage for different data types
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace astra::core::memory {

// Object size estimator - estimates memory usage for Redis data types
class ObjectSizeEstimator {
 public:
  ObjectSizeEstimator() = default;
  ~ObjectSizeEstimator() = default;

  // ========== String Size Estimation ==========

  // Estimate String value size
  // StringValue overhead + key size + value size
  static uint32_t EstimateStringSize(const std::string& key,
                                     const std::string& value) {
    // StringValue struct overhead (approximately)
    constexpr uint32_t kStringValueOverhead = 24;  // std::string overhead

    // DashMap entry overhead
    constexpr uint32_t kDashMapEntryOverhead = 32;  // key + value pointers + hash

    // Total: StringValue overhead + key size + value size + DashMap entry
    return kStringValueOverhead + key.size() + value.size() +
           kDashMapEntryOverhead;
  }

  // Estimate String replacement size (delta)
  static int32_t EstimateStringDelta(const std::string& key,
                                     const std::string& old_value,
                                     const std::string& new_value) {
    int64_t old_size = EstimateStringSize(key, old_value);
    int64_t new_size = EstimateStringSize(key, new_value);
    return static_cast<int32_t>(new_size - old_size);
  }

  // ========== Hash Size Estimation ==========

  // Estimate Hash size
  // HashType overhead + sum of (field size + value size) for all fields
  static uint32_t EstimateHashSize(
      const std::string& key,
      const std::vector<std::pair<std::string, std::string>>& fields) {
    // HashType overhead (DashMap shared_ptr + internal structure)
    constexpr uint32_t kHashTypeOverhead = 64;

    // DashMap entry overhead
    constexpr uint32_t kDashMapEntryOverhead = 32;

    // Calculate total size for all fields
    uint32_t fields_size = 0;
    for (const auto& [field, value] : fields) {
      // Each field-value pair entry in DashMap
      fields_size += field.size() + value.size() + kDashMapEntryOverhead;
    }

    // Total: HashType overhead + fields size + key entry
    return kHashTypeOverhead + fields_size + key.size() + kDashMapEntryOverhead;
  }

  // Estimate single Hash field addition size
  static uint32_t EstimateHashFieldSize(const std::string& field,
                                        const std::string& value) {
    constexpr uint32_t kDashMapEntryOverhead = 32;
    return field.size() + value.size() + kDashMapEntryOverhead;
  }

  // ========== Set Size Estimation ==========

  // Estimate Set size
  // SetType overhead + sum of member sizes
  static uint32_t EstimateSetSize(
      const std::string& key, const std::vector<std::string>& members) {
    // SetType overhead (DashSet shared_ptr + internal structure)
    constexpr uint32_t kSetTypeOverhead = 64;

    // DashMap entry overhead
    constexpr uint32_t kDashMapEntryOverhead = 32;

    // Calculate total size for all members
    uint32_t members_size = 0;
    for (const auto& member : members) {
      members_size += member.size() + kDashMapEntryOverhead;
    }

    // Total: SetType overhead + members size + key entry
    return kSetTypeOverhead + members_size + key.size() + kDashMapEntryOverhead;
  }

  // Estimate single Set member addition size
  static uint32_t EstimateSetMemberSize(const std::string& member) {
    constexpr uint32_t kDashMapEntryOverhead = 32;
    return member.size() + kDashMapEntryOverhead;
  }

  // ========== Sorted Set Size Estimation ==========

  // Estimate ZSet size
  // ZSetType overhead + sum of (member size + score size) for all members
  static uint32_t EstimateZSetSize(
      const std::string& key,
      const std::vector<std::pair<std::string, double>>& members) {
    // ZSetType overhead (shared_ptr + B-tree structure)
    constexpr uint32_t kBTreeZSetOverhead = 128;

    // DashMap entry overhead for key
    constexpr uint32_t kDashMapEntryOverhead = 32;

    // Calculate total size for all members
    uint32_t members_size = 0;
    for (const auto& [member, score] : members) {
      (void)score;  // Score is double (8 bytes) stored inline
      // Each member: string + overhead
      constexpr uint32_t kZSetMemberOverhead = 24;  // B-tree node overhead
      members_size += member.size() + kZSetMemberOverhead + sizeof(double);
    }

    // Total: ZSetType overhead + members size + key entry
    return kBTreeZSetOverhead + members_size + key.size() + kDashMapEntryOverhead;
  }

  // Estimate single ZSet member addition size
  static uint32_t EstimateZSetMemberSize(const std::string& member) {
    constexpr uint32_t kZSetMemberOverhead = 24;  // B-tree node overhead
    return member.size() + kZSetMemberOverhead + sizeof(double);
  }

  // ========== List Size Estimation ==========

  // Estimate List size
  // ListType overhead + sum of (node overhead + element size) for all elements
  static uint32_t EstimateListSize(const std::string& key,
                                   const std::vector<std::string>& elements) {
    // ListType overhead (shared_ptr + list structure)
    constexpr uint32_t kListTypeOverhead = 64;

    // DashMap entry overhead for key
    constexpr uint32_t kDashMapEntryOverhead = 32;

    // Calculate total size for all elements
    uint32_t elements_size = 0;
    for (const auto& element : elements) {
      // Each element: node overhead + element size
      constexpr uint32_t kListNodeOverhead = 16;  // Doubly-linked list node
      elements_size += kListNodeOverhead + element.size();
    }

    // Total: ListType overhead + elements size + key entry
    return kListTypeOverhead + elements_size + key.size() + kDashMapEntryOverhead;
  }

  // Estimate single List element addition size
  static uint32_t EstimateListElementSize(const std::string& element) {
    constexpr uint32_t kListNodeOverhead = 16;  // Doubly-linked list node
    return kListNodeOverhead + element.size();
  }

  // ========== Stream Size Estimation ==========

  // Estimate Stream size
  // StreamData overhead + sum of entry sizes
  static uint32_t EstimateStreamSize(
      const std::string& key,
      const std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>>& entries) {
    // StreamData overhead (shared_ptr + stream structure)
    constexpr uint32_t kStreamDataOverhead = 128;

    // DashMap entry overhead for key
    constexpr uint32_t kDashMapEntryOverhead = 32;

    // Calculate total size for all entries
    uint32_t entries_size = 0;
    for (const auto& [entry_id, fields] : entries) {
      // Entry ID size
      entries_size += entry_id.size();

      // Each entry: overhead + fields
      constexpr uint32_t kStreamEntryOverhead = 32;
      entries_size += kStreamEntryOverhead;

      // Calculate size for all fields
      for (const auto& [field, value] : fields) {
        entries_size += field.size() + value.size();
      }
    }

    // Total: StreamData overhead + entries size + key entry
    return kStreamDataOverhead + entries_size + key.size() + kDashMapEntryOverhead;
  }

  // Estimate single Stream entry addition size
  static uint32_t EstimateStreamEntrySize(
      const std::string& entry_id,
      const std::vector<std::pair<std::string, std::string>>& fields) {
    constexpr uint32_t kStreamEntryOverhead = 32;

    uint32_t size = entry_id.size() + kStreamEntryOverhead;
    for (const auto& [field, value] : fields) {
      size += field.size() + value.size();
    }

    return size;
  }

  // ========== Key Metadata Estimation ==========

  // Estimate KeyMetadata size
  static uint32_t EstimateMetadataSize(const std::string& key) {
    // KeyMetadata struct size
    constexpr uint32_t kKeyMetadataOverhead = 48;  // All fields

    // DashMap entry overhead
    constexpr uint32_t kDashMapEntryOverhead = 32;

    return kKeyMetadataOverhead + key.size() + kDashMapEntryOverhead;
  }

  // ========== Utility Functions ==========

  // Estimate total size for a key (metadata + data)
  template <typename T>
  static uint32_t EstimateTotalSize(const std::string& key, const T& data) {
    return EstimateMetadataSize(key) + data;
  }
};

}  // namespace astra::core::memory