// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "hyperloglog_commands.hpp"
#include "command_auto_register.hpp"
#include "astra/base/logging.hpp"
#include <absl/strings/ascii.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace astra::commands {

// ==============================================================================
// HyperLogLog Implementation
// ==============================================================================
// HyperLogLog uses a 64-bit hash divided into:
// - First 14 bits: register index (2^14 = 16384 registers)
// - Remaining 50 bits: used to count leading zeros
// Each register stores the count of leading zeros (max 50)
// Stored as 6 bits per register = 16384 * 6 / 8 = 12288 bytes per HLL

constexpr size_t kHllRegisters = 16384;  // 2^14 registers
constexpr size_t kHllBitsPerRegister = 6;  // Store up to 50 leading zeros
constexpr size_t kHllBytes = (kHllRegisters * kHllBitsPerRegister) / 8;  // 12288 bytes

// Helper: Get a 64-bit hash of a string (simplified MurmurHash3)
static uint64_t MurmurHash64(const std::string& key) {
  const uint64_t seed = 0x9747b28c;
  const uint64_t m = 0xc6a4a7935bd1e995;
  const int r = 47;
  
  uint64_t h = seed ^ (key.size() * m);
  
  const uint64_t* data = reinterpret_cast<const uint64_t*>(key.data());
  size_t blocks = key.size() / 8;
  
  for (size_t i = 0; i < blocks; ++i) {
    uint64_t k = data[i];
    k *= m;
    k ^= k >> r;
    k *= m;
    h ^= k;
    h *= m;
  }
  
  const uint8_t* tail = reinterpret_cast<const uint8_t*>(key.data() + blocks * 8);
  uint64_t k = 0;
  switch (key.size() & 7) {
    case 7: k ^= static_cast<uint64_t>(tail[6]) << 48;
    case 6: k ^= static_cast<uint64_t>(tail[5]) << 40;
    case 5: k ^= static_cast<uint64_t>(tail[4]) << 32;
    case 4: k ^= static_cast<uint64_t>(tail[3]) << 24;
    case 3: k ^= static_cast<uint64_t>(tail[2]) << 16;
    case 2: k ^= static_cast<uint64_t>(tail[1]) << 8;
    case 1: k ^= static_cast<uint64_t>(tail[0]);
      k *= m;
      k ^= k >> r;
      k *= m;
      h ^= k;
  }
  
  h ^= h >> r;
  h *= m;
  h ^= h >> r;
  
  return h;
}

// Helper: Count leading zeros in a 64-bit value (excluding the first bit)
static inline uint8_t CountLeadingZeros(uint64_t value) {
  // Remove the first bit (used for register index)
  value &= 0x00FFFFFFFFFFFFFFULL;
  
  if (value == 0) {
    return 50;  // Maximum possible
  }
  
  uint8_t count = 0;
  while ((value & (1ULL << 49)) == 0) {
    count++;
    value <<= 1;
  }
  
  return count;
}

// Helper: Get register value from HLL data
static inline uint8_t GetRegister(const std::string& hll_data, size_t idx) {
  size_t bit_pos = idx * kHllBitsPerRegister;
  size_t byte_pos = bit_pos / 8;
  size_t bit_offset = bit_pos % 8;
  
  uint16_t value = static_cast<uint8_t>(hll_data[byte_pos]) |
                  (static_cast<uint8_t>(hll_data[byte_pos + 1]) << 8);
  
  return (value >> bit_offset) & 0x3F;
}

// Helper: Set register value in HLL data
static inline void SetRegister(std::string& hll_data, size_t idx, uint8_t value) {
  size_t bit_pos = idx * kHllBitsPerRegister;
  size_t byte_pos = bit_pos / 8;
  size_t bit_offset = bit_pos % 8;
  
  uint16_t mask = 0x3F << bit_offset;
  uint16_t new_value = (static_cast<uint16_t>(hll_data[byte_pos]) |
                       (static_cast<uint16_t>(hll_data[byte_pos + 1]) << 8));
  new_value = (new_value & ~mask) | ((value & 0x3F) << bit_offset);
  
  hll_data[byte_pos] = new_value & 0xFF;
  hll_data[byte_pos + 1] = (new_value >> 8) & 0xFF;
}

// Helper: Ensure HLL data is initialized
static std::string& EnsureHllData(std::string& hll_data) {
  if (hll_data.size() != kHllBytes) {
    hll_data.resize(kHllBytes, '\0');
  }
  return hll_data;
}

// Helper: Estimate cardinality using HyperLogLog algorithm
static uint64_t EstimateCardinality(const std::string& hll_data) {
  if (hll_data.size() != kHllBytes) {
    return 0;
  }
  
  double sum = 0.0;
  uint64_t zero_count = 0;
  
  for (size_t i = 0; i < kHllRegisters; ++i) {
    uint8_t reg = GetRegister(hll_data, i);
    sum += 1.0 / (1ULL << reg);
    if (reg == 0) {
      zero_count++;
    }
  }
  
  double alpha = 0.7213 / (1.0 + 1.079 / kHllRegisters);
  double estimate = alpha * kHllRegisters * kHllRegisters / sum;
  
  // Correction for small and large ranges
  if (estimate <= 2.5 * kHllRegisters && zero_count > 0) {
    estimate = kHllRegisters * log(static_cast<double>(kHllRegisters) / zero_count);
  } else if (estimate > (1ULL << 32) * 30) {
    estimate = -(1ULL << 32) * log(1.0 - estimate / (1ULL << 32));
  }
  
  return static_cast<uint64_t>(estimate);
}

// PFADD key [element ...] - Adds elements to a HyperLogLog key
CommandResult HandlePfAdd(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'pfadd' command");
  }

  const std::string& key = command[0].AsString();
  auto db = context->GetDatabase();
  
  // Get or create HLL data
  auto value = db->Get(key);
  std::string hll_data;
  if (value.has_value()) {
    hll_data = value->value;
  }
  EnsureHllData(hll_data);
  
  // Process each element
  for (size_t i = 1; i < command.ArgCount(); ++i) {
    const std::string& element = command[i].AsString();
    uint64_t hash = MurmurHash64(element);
    
    // Extract register index (first 14 bits)
    size_t reg_idx = hash >> 50;
    
    // Count leading zeros in remaining bits
    uint8_t count = CountLeadingZeros(hash);
    
    // Update register if needed
    uint8_t current = GetRegister(hll_data, reg_idx);
    if (count > current) {
      SetRegister(hll_data, reg_idx, count);
    }
  }
  
  // Store updated HLL data
  db->Set(key, hll_data);
  
  protocol::RespValue resp;
  resp.SetInteger(1);  // Always return 1 (at least one register was modified)
  return CommandResult(resp);
}

// PFCOUNT key [key ...] - Returns the approximated cardinality
CommandResult HandlePfCount(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'pfcount' command");
  }

  // If only one key, estimate directly
  if (command.ArgCount() == 1) {
    const std::string& key = command[0].AsString();
    auto db = context->GetDatabase();
    auto value = db->Get(key);
    
    if (!value.has_value() || value->value.size() != kHllBytes) {
      protocol::RespValue resp;
      resp.SetInteger(0);
      return CommandResult(resp);
    }
    
    uint64_t estimate = EstimateCardinality(value->value);
    protocol::RespValue resp;
    resp.SetInteger(estimate);
    return CommandResult(resp);
  }
  
  // Multiple keys: merge them first
  std::string merged_hll(kHllBytes, '\0');
  auto db = context->GetDatabase();
  
  for (size_t i = 0; i < command.ArgCount(); ++i) {
    const std::string& key = command[i].AsString();
    auto value = db->Get(key);
    
    if (value.has_value() && value->value.size() == kHllBytes) {
      // Merge: take max of each register
      for (size_t j = 0; j < kHllRegisters; ++j) {
        uint8_t reg = GetRegister(value->value, j);
        uint8_t current = GetRegister(merged_hll, j);
        if (reg > current) {
          SetRegister(merged_hll, j, reg);
        }
      }
    }
  }
  
  uint64_t estimate = EstimateCardinality(merged_hll);
  protocol::RespValue resp;
  resp.SetInteger(estimate);
  return CommandResult(resp);
}

// PFMERGE destkey sourcekey [sourcekey ...] - Merges multiple HyperLogLog values
CommandResult HandlePfMerge(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'pfmerge' command");
  }

  const std::string& destkey = command[0].AsString();
  auto db = context->GetDatabase();
  
  // Initialize merged HLL
  std::string merged_hll(kHllBytes, '\0');
  
  // Merge all source keys
  for (size_t i = 1; i < command.ArgCount(); ++i) {
    const std::string& key = command[i].AsString();
    auto value = db->Get(key);
    
    if (value.has_value() && value->value.size() == kHllBytes) {
      // Merge: take max of each register
      for (size_t j = 0; j < kHllRegisters; ++j) {
        uint8_t reg = GetRegister(value->value, j);
        uint8_t current = GetRegister(merged_hll, j);
        if (reg > current) {
          SetRegister(merged_hll, j, reg);
        }
      }
    }
  }
  
  // Store merged result
  db->Set(destkey, merged_hll);
  
  protocol::RespValue resp;
  resp.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(resp);
}

// PFDEBUG subkey key [arguments...] - Debug HyperLogLog internal structures
CommandResult HandlePfDebug(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'PFDEBUG' command");
  }

  std::string subcommand = absl::AsciiStrToUpper(command[0].AsString());
  std::string key = command[1].AsString();
  
  auto db = context->GetDatabase();
  auto value = db->Get(key);
  
  if (!value.has_value() || value->value.size() != kHllBytes) {
    return CommandResult(false, "ERR key does not contain a valid HyperLogLog structure");
  }
  
  const std::string& hll_data = value->value;
  
  if (subcommand == "GETREG") {
    // Get register value: PFDEBUG GETREG key index
    if (command.ArgCount() != 3) {
      return CommandResult(false, "ERR wrong number of arguments for 'PFDEBUG GETREG' command");
    }
    
    int64_t idx;
    if (!absl::SimpleAtoi(command[2].AsString(), &idx) || idx < 0 || idx >= static_cast<int64_t>(kHllRegisters)) {
      return CommandResult(false, "ERR invalid register index");
    }
    
    uint8_t reg = GetRegister(hll_data, idx);
    protocol::RespValue resp;
    resp.SetInteger(reg);
    return CommandResult(resp);
    
  } else if (subcommand == "DENSE") {
    // Show dense representation of registers
    std::vector<protocol::RespValue> result;
    for (size_t i = 0; i < kHllRegisters; ++i) {
      uint8_t reg = GetRegister(hll_data, i);
      result.emplace_back(static_cast<int64_t>(reg));
    }
    
    protocol::RespValue resp;
    resp.SetArray(std::move(result));
    return CommandResult(resp);
    
  } else if (subcommand == "ENCODING") {
    // Show encoding information
    std::vector<protocol::RespValue> result;
    result.emplace_back("dense");  // Always dense for our implementation
    result.emplace_back(static_cast<int64_t>(kHllBytes));
    result.emplace_back(static_cast<int64_t>(kHllRegisters));
    
    protocol::RespValue resp;
    resp.SetArray(std::move(result));
    return CommandResult(resp);
    
  } else {
    return CommandResult(false, "ERR unknown PFDEBUG subcommand '" + subcommand + "'");
  }
}

// PFSELFTEST - Test HyperLogLog implementation
CommandResult HandlePfSelfTest(const protocol::Command& command, CommandContext* context) {
  // Simple self-test: add known elements and check cardinality
  [[maybe_unused]] Database* db = context->GetDatabase();
  
  // Test 1: Empty HLL should return 0
  std::string empty_hll(kHllBytes, '\0');
  uint64_t empty_estimate = EstimateCardinality(empty_hll);
  if (empty_estimate != 0) {
    return CommandResult(false, "ERR PFSELFTEST failed: empty HLL should estimate 0");
  }
  
  // Test 2: HLL with 1000 elements should estimate ~1000 (with some error margin)
  std::string test_hll(kHllBytes, '\0');
  for (int i = 0; i < 1000; ++i) {
    std::string element = "element_" + std::to_string(i);
    uint64_t hash = MurmurHash64(element);
    size_t reg_idx = hash >> 50;
    uint8_t count = CountLeadingZeros(hash);
    uint8_t current = GetRegister(test_hll, reg_idx);
    if (count > current) {
      SetRegister(test_hll, reg_idx, count);
    }
  }
  
  uint64_t test_estimate = EstimateCardinality(test_hll);
  // Allow 20% error margin
  if (test_estimate < 800 || test_estimate > 1200) {
    return CommandResult(false, "ERR PFSELFTEST failed: estimate out of range");
  }
  
  // All tests passed
  protocol::RespValue resp;
  resp.SetString("PASSED", protocol::RespType::kSimpleString);
  return CommandResult(resp);
}

// Auto-register all HyperLogLog commands
ASTRADB_REGISTER_COMMAND(PFADD, -2, "write", RoutingStrategy::kByFirstKey, HandlePfAdd);
ASTRADB_REGISTER_COMMAND(PFCOUNT, -2, "readonly", RoutingStrategy::kByFirstKey, HandlePfCount);
ASTRADB_REGISTER_COMMAND(PFMERGE, -2, "write", RoutingStrategy::kByFirstKey, HandlePfMerge);
ASTRADB_REGISTER_COMMAND(PFDEBUG, -3, "readonly", RoutingStrategy::kByFirstKey, HandlePfDebug);
ASTRADB_REGISTER_COMMAND(PFSELFTEST, 1, "readonly", RoutingStrategy::kNone, HandlePfSelfTest);

}  // namespace astra::commands