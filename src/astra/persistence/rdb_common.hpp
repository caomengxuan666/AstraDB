// ==============================================================================
// RDB Common Definitions
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <cstdint>
#include <string>
#include <tuple>

namespace astra {

// Key type enumeration (duplicated from storage::KeyType to avoid circular dependency)
enum class KeyType : uint8_t {
  kNone = 0,
  kString = 1,
  kHash = 2,
  kSet = 3,
  kZSet = 4,
  kList = 5,
  kStream = 6
};

}  // namespace astra

namespace astra::storage {
// Forward declaration to avoid circular dependency
enum class KeyType : uint8_t;
}

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

// Convert KeyType to RDB type
inline uint8_t KeyTypeToRdbType(astra::KeyType key_type) {
  switch (key_type) {
    case astra::KeyType::kString: return RDB_TYPE_STRING;
    case astra::KeyType::kHash: return RDB_TYPE_HASH;
    case astra::KeyType::kList: return RDB_TYPE_LIST;
    case astra::KeyType::kSet: return RDB_TYPE_SET;
    case astra::KeyType::kZSet: return RDB_TYPE_ZSET;
    case astra::KeyType::kStream: return RDB_TYPE_HASH;  // Stream not supported, use hash
    default: return RDB_TYPE_STRING;  // Default to string
  }
}

// Convert astra::storage::KeyType to RDB type (for compatibility)
inline uint8_t KeyTypeToRdbType(astra::storage::KeyType key_type) {
  uint8_t val = static_cast<uint8_t>(key_type);
  switch (val) {
    case 1: return RDB_TYPE_STRING;  // kString
    case 2: return RDB_TYPE_HASH;    // kHash
    case 5: return RDB_TYPE_LIST;    // kList
    case 3: return RDB_TYPE_SET;     // kSet
    case 4: return RDB_TYPE_ZSET;    // kZSet
    case 6: return RDB_TYPE_HASH;    // kStream -> HASH
    default: return RDB_TYPE_STRING;  // Default to string
  }
}

// Convert RDB type to KeyType
inline astra::KeyType RdbTypeToKeyType(uint8_t rdb_type) {
  switch (rdb_type) {
    case RDB_TYPE_STRING: return astra::KeyType::kString;
    case RDB_TYPE_HASH: return astra::KeyType::kHash;
    case RDB_TYPE_LIST: return astra::KeyType::kList;
    case RDB_TYPE_SET: return astra::KeyType::kSet;
    case RDB_TYPE_ZSET: return astra::KeyType::kZSet;
    default: return astra::KeyType::kString;  // Default to string
  }
}

// RDB key-value pair
struct RdbKeyValue {
  uint8_t type;
  std::string key;
  std::string value;
  int64_t expire_ms;  // Absolute expire time in milliseconds
};

}  // namespace astra::persistence