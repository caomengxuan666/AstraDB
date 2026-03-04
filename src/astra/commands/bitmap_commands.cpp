// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "bitmap_commands.hpp"
#include "command_auto_register.hpp"
#include "astra/base/logging.hpp"
#include <algorithm>
#include <stdexcept>

namespace astra::commands {

// Helper: Count set bits in a byte (population count)
static inline uint8_t PopCount(uint8_t byte) {
  uint8_t count = 0;
  while (byte) {
    count += byte & 1;
    byte >>= 1;
  }
  return count;
}

// Helper: Ensure bitmap has enough bytes for the given bit offset
static void EnsureBitmapSize(std::string& bitmap, int64_t bit_offset) {
  int64_t required_bytes = (bit_offset / 8) + 1;
  if (static_cast<int64_t>(bitmap.size()) < required_bytes) {
    bitmap.resize(required_bytes, '\0');
  }
}

// Helper: Get bit at offset
static bool GetBit(const std::string& bitmap, int64_t offset) {
  if (offset < 0) {
    return false;
  }
  int64_t byte_offset = offset / 8;
  int bit_in_byte = offset % 8;
  if (byte_offset >= static_cast<int64_t>(bitmap.size())) {
    return false;
  }
  return (bitmap[byte_offset] >> bit_in_byte) & 1;
}

// Helper: Set bit at offset
static void SetBit(std::string& bitmap, int64_t offset, bool value) {
  if (offset < 0) {
    return;
  }
  EnsureBitmapSize(bitmap, offset);
  int64_t byte_offset = offset / 8;
  int bit_in_byte = offset % 8;
  if (value) {
    bitmap[byte_offset] |= (1 << bit_in_byte);
  } else {
    bitmap[byte_offset] &= ~(1 << bit_in_byte);
  }
}

// BITCOUNT key [start end] - Count set bits in a string
CommandResult HandleBitCount(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 1 || command.ArgCount() > 3) {
    return CommandResult(false, "ERR wrong number of arguments for 'bitcount' command");
  }

  const std::string& key = command[0].AsString();
  auto db = context->GetDatabase();

  auto value = db->Get(key);
  if (!value.has_value()) {
    protocol::RespValue resp;
    resp.SetInteger(0);
    return CommandResult(resp);
  }

  const std::string& bitmap = value->value;
  int64_t start = 0;
  int64_t end = static_cast<int64_t>(bitmap.size()) - 1;

  // Parse start and end indices
  if (command.ArgCount() >= 2) {
    try {
      start = std::stoll(command[1].AsString());
    } catch (...) {
      return CommandResult(false, "ERR value is not an integer or out of range");
    }
  }
  if (command.ArgCount() >= 3) {
    try {
      end = std::stoll(command[2].AsString());
    } catch (...) {
      return CommandResult(false, "ERR value is not an integer or out of range");
    }
  }

  // Handle negative indices
  int64_t len = static_cast<int64_t>(bitmap.size());
  if (start < 0) start = len + start;
  if (end < 0) end = len + end;

  // Clamp to valid range
  if (start < 0) start = 0;
  if (end >= len) end = len - 1;

  // Count set bits
  uint64_t count = 0;
  for (int64_t i = start; i <= end && i < len; ++i) {
    count += PopCount(static_cast<uint8_t>(bitmap[i]));
  }

  protocol::RespValue resp;
  resp.SetInteger(count);
  return CommandResult(resp);
}

// BITOP operation destkey key [key ...] - Perform bitwise operations between strings
CommandResult HandleBitOp(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 3) {
    return CommandResult(false, "ERR wrong number of arguments for 'bitop' command");
  }

  const std::string& operation = command[0].AsString();
  const std::string& destkey = command[1].AsString();

  // Get source bitmaps
  std::vector<std::string> sources;
  size_t max_len = 0;

  for (size_t i = 2; i < command.ArgCount(); ++i) {
    const std::string& key = command[i].AsString();
    auto db = context->GetDatabase();
    auto value = db->Get(key);
    if (value.has_value()) {
      sources.push_back(value->value);
      max_len = std::max(max_len, sources.back().size());
    } else {
      sources.push_back("");
    }
  }

  // Perform operation
  std::string result;
  result.resize(max_len, '\0');

  if (operation == "AND") {
    for (size_t i = 0; i < max_len; ++i) {
      uint8_t byte = 0xFF;
      for (const auto& source : sources) {
        if (i < source.size()) {
          byte &= static_cast<uint8_t>(source[i]);
        } else {
          byte &= 0;
        }
      }
      result[i] = static_cast<char>(byte);
    }
  } else if (operation == "OR") {
    for (size_t i = 0; i < max_len; ++i) {
      uint8_t byte = 0;
      for (const auto& source : sources) {
        if (i < source.size()) {
          byte |= static_cast<uint8_t>(source[i]);
        }
      }
      result[i] = static_cast<char>(byte);
    }
  } else if (operation == "XOR") {
    for (size_t i = 0; i < max_len; ++i) {
      uint8_t byte = 0;
      for (const auto& source : sources) {
        if (i < source.size()) {
          byte ^= static_cast<uint8_t>(source[i]);
        }
      }
      result[i] = static_cast<char>(byte);
    }
  } else if (operation == "NOT") {
    if (sources.size() != 1) {
      return CommandResult(false, "ERR BITOP NOT must be called with a single source key");
    }
    for (size_t i = 0; i < max_len; ++i) {
      result[i] = ~sources[0][i];
    }
  } else {
    return CommandResult(false, "ERR syntax error, unknown BITOP operation");
  }

  // Store result
  auto db = context->GetDatabase();
  db->Set(destkey, result);

  protocol::RespValue resp;
  resp.SetInteger(result.size());
  return CommandResult(resp);
}

// BITPOS key bit [start end] - Find first bit set or clear in a string
CommandResult HandleBitPos(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 2 || command.ArgCount() > 4) {
    return CommandResult(false, "ERR wrong number of arguments for 'bitpos' command");
  }

  const std::string& key = command[0].AsString();
  int64_t bit_value;
  try {
    bit_value = std::stoll(command[1].AsString());
    if (bit_value != 0 && bit_value != 1) {
      return CommandResult(false, "ERR bit value must be 0 or 1");
    }
  } catch (...) {
    return CommandResult(false, "ERR bit value must be 0 or 1");
  }

  auto db = context->GetDatabase();
  auto value = db->Get(key);
  std::string bitmap;
  if (value.has_value()) {
    bitmap = value->value;
  }

  int64_t start = 0;
  int64_t end = static_cast<int64_t>(bitmap.size()) - 1;

  if (command.ArgCount() >= 3) {
    try {
      start = std::stoll(command[2].AsString());
    } catch (...) {
      return CommandResult(false, "ERR value is not an integer or out of range");
    }
  }
  if (command.ArgCount() >= 4) {
    try {
      end = std::stoll(command[3].AsString());
    } catch (...) {
      return CommandResult(false, "ERR value is not an integer or out of range");
    }
  }

  // Handle negative indices
  int64_t len = static_cast<int64_t>(bitmap.size());
  if (start < 0) start = len + start;
  if (end < 0) end = len + end;

  // Clamp to valid range
  if (start < 0) start = 0;
  if (end >= len) end = len - 1;

  // Find the bit
  bool target_bit = (bit_value == 1);
  for (int64_t byte_idx = start; byte_idx <= end && byte_idx < len; ++byte_idx) {
    uint8_t byte = static_cast<uint8_t>(bitmap[byte_idx]);
    for (int bit_idx = 0; bit_idx < 8; ++bit_idx) {
      bool current_bit = (byte >> bit_idx) & 1;
      if (current_bit == target_bit) {
        protocol::RespValue resp;
        resp.SetInteger(byte_idx * 8 + bit_idx);
        return CommandResult(resp);
      }
    }
  }

  // Bit not found
  protocol::RespValue resp;
  resp.SetInteger(-1);
  return CommandResult(resp);
}

// BITFIELD key [GET encoding offset] [SET encoding offset value] [INCRBY encoding offset increment] [OVERFLOW WRAP|SAT|FAIL]
CommandResult HandleBitField(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'bitfield' command");
  }

  const std::string& key = command[0].AsString();
  auto db = context->GetDatabase();

  // Get or create bitmap
  auto value = db->Get(key);
  std::string bitmap;
  if (value.has_value()) {
    bitmap = value->value;
  }

  std::vector<protocol::RespValue> results;
  std::string overflow_mode = "WRAP";  // Default overflow mode

  size_t i = 1;
  while (i < command.ArgCount()) {
    const std::string& subcommand = command[i].AsString();

    if (subcommand == "OVERFLOW") {
      if (i + 1 >= command.ArgCount()) {
        return CommandResult(false, "ERR wrong number of arguments for 'BITFIELD ... OVERFLOW'");
      }
      overflow_mode = command[i + 1].AsString();
      if (overflow_mode != "WRAP" && overflow_mode != "SAT" && overflow_mode != "FAIL") {
        return CommandResult(false, "ERR OVERFLOW only supports WRAP, SAT, FAIL");
      }
      i += 2;
      continue;
    }

    if (subcommand == "GET") {
      if (i + 2 >= command.ArgCount()) {
        return CommandResult(false, "ERR wrong number of arguments for 'BITFIELD GET'");
      }

      // Parse encoding (e.g., "u8", "i16")
      const std::string& encoding = command[i + 1].AsString();
      int64_t offset;
      try {
        offset = std::stoll(command[i + 2].AsString());
      } catch (...) {
        return CommandResult(false, "ERR offset is not an integer or out of range");
      }

      // Extract type and bits
      bool signed_type = (encoding[0] == 'i');
      int bits = std::stoi(encoding.substr(1));

      if (bits <= 0 || bits > 64) {
        return CommandResult(false, "ERR invalid bitfield type");
      }

      // Calculate byte offset
      int64_t byte_offset = (offset * bits) / 8;
      int bit_offset = (offset * bits) % 8;

      // Ensure bitmap is large enough
      int64_t required_bytes = byte_offset + (bits + 7) / 8;
      if (static_cast<int64_t>(bitmap.size()) < required_bytes) {
        protocol::RespValue resp;
        resp.SetInteger(0);
        results.push_back(resp);
      } else {
        // Extract the field
        uint64_t field_value = 0;
        for (int j = 0; j < bits; ++j) {
          int64_t abs_offset = byte_offset * 8 + bit_offset + j;
          if (abs_offset < static_cast<int64_t>(bitmap.size() * 8)) {
            bool bit = GetBit(bitmap, abs_offset);
            if (bit) {
              field_value |= (1ULL << j);
            }
          }
        }

        // Handle signed types
        if (signed_type && bits < 64) {
          uint64_t sign_bit = 1ULL << (bits - 1);
          if (field_value & sign_bit) {
            field_value |= ~((1ULL << bits) - 1);
          }
        }

        protocol::RespValue resp;
        resp.SetInteger(static_cast<int64_t>(field_value));
        results.push_back(resp);
      }

      i += 3;
    } else if (subcommand == "SET") {
      if (i + 3 >= command.ArgCount()) {
        return CommandResult(false, "ERR wrong number of arguments for 'BITFIELD SET'");
      }

      const std::string& encoding = command[i + 1].AsString();
      int64_t offset;
      int64_t new_value;
      try {
        offset = std::stoll(command[i + 2].AsString());
        new_value = std::stoll(command[i + 3].AsString());
      } catch (...) {
        return CommandResult(false, "ERR offset or value is not an integer or out of range");
      }

      bool signed_type = (encoding[0] == 'i');
      int bits = std::stoi(encoding.substr(1));

      if (bits <= 0 || bits > 64) {
        return CommandResult(false, "ERR invalid bitfield type");
      }

      // Check overflow
      if (overflow_mode == "FAIL") {
        uint64_t max_value = (1ULL << bits) - 1;
        if (signed_type) {
          int64_t min_value = -(1LL << (bits - 1));
          int64_t max_signed_value = (1LL << (bits - 1)) - 1;
          if (new_value < min_value || new_value > max_signed_value) {
            results.emplace_back(protocol::RespValue(protocol::RespType::kNullBulkString));
            i += 4;
            continue;
          }
        } else {
          if (new_value < 0 || static_cast<uint64_t>(new_value) > max_value) {
            results.emplace_back(protocol::RespValue(protocol::RespType::kNullBulkString));
            i += 4;
            continue;
          }
        }
      } else if (overflow_mode == "SAT") {
        uint64_t max_value = (1ULL << bits) - 1;
        if (signed_type) {
          int64_t min_value = -(1LL << (bits - 1));
          int64_t max_signed_value = (1LL << (bits - 1)) - 1;
          if (new_value < min_value) new_value = min_value;
          if (new_value > max_signed_value) new_value = max_signed_value;
        } else {
          if (new_value < 0) new_value = 0;
          if (static_cast<uint64_t>(new_value) > max_value) new_value = max_value;
        }
      }

      // Set the bits
      uint64_t value_to_set = static_cast<uint64_t>(new_value);
      for (int j = 0; j < bits; ++j) {
        int64_t abs_offset = (offset * bits) + j;
        bool bit = (value_to_set >> j) & 1;
        SetBit(bitmap, abs_offset, bit);
      }

      protocol::RespValue resp;
      resp.SetInteger(new_value);
      results.push_back(resp);

      i += 4;
    } else if (subcommand == "INCRBY") {
      if (i + 3 >= command.ArgCount()) {
        return CommandResult(false, "ERR wrong number of arguments for 'BITFIELD INCRBY'");
      }

      const std::string& encoding = command[i + 1].AsString();
      int64_t offset;
      int64_t increment;
      try {
        offset = std::stoll(command[i + 2].AsString());
        increment = std::stoll(command[i + 3].AsString());
      } catch (...) {
        return CommandResult(false, "ERR offset or increment is not an integer or out of range");
      }

      bool signed_type = (encoding[0] == 'i');
      int bits = std::stoi(encoding.substr(1));

      if (bits <= 0 || bits > 64) {
        return CommandResult(false, "ERR invalid bitfield type");
      }

      // Get current value
      int64_t byte_offset = (offset * bits) / 8;
      int bit_offset = (offset * bits) % 8;

      int64_t required_bytes = byte_offset + (bits + 7) / 8;
      if (static_cast<int64_t>(bitmap.size()) < required_bytes) {
        bitmap.resize(required_bytes, '\0');
      }

      uint64_t current_value = 0;
      for (int j = 0; j < bits; ++j) {
        int64_t abs_offset = byte_offset * 8 + bit_offset + j;
        bool bit = GetBit(bitmap, abs_offset);
        if (bit) {
          current_value |= (1ULL << j);
        }
      }

      // Handle signed types for current value
      int64_t signed_current = static_cast<int64_t>(current_value);
      if (signed_type && bits < 64) {
        uint64_t sign_bit = 1ULL << (bits - 1);
        if (current_value & sign_bit) {
          signed_current = static_cast<int64_t>(current_value | ~((1ULL << bits) - 1));
        }
      }

      // Calculate new value
      int64_t new_value = signed_current + increment;

      // Handle overflow
      bool overflow = false;
      if (overflow_mode == "FAIL") {
        if (signed_type) {
          int64_t min_value = -(1LL << (bits - 1));
          int64_t max_signed_value = (1LL << (bits - 1)) - 1;
          if (new_value < min_value || new_value > max_signed_value) {
            overflow = true;
          }
        } else {
          uint64_t max_value = (1ULL << bits) - 1;
          if (new_value < 0 || static_cast<uint64_t>(new_value) > max_value) {
            overflow = true;
          }
        }
      } else if (overflow_mode == "SAT") {
        if (signed_type) {
          int64_t min_value = -(1LL << (bits - 1));
          int64_t max_signed_value = (1LL << (bits - 1)) - 1;
          if (new_value < min_value) new_value = min_value;
          if (new_value > max_signed_value) new_value = max_signed_value;
        } else {
          uint64_t max_value = (1ULL << bits) - 1;
          if (new_value < 0) new_value = 0;
          if (static_cast<uint64_t>(new_value) > max_value) new_value = max_value;
        }
      } else {  // WRAP
        uint64_t mask = (1ULL << bits) - 1;
        new_value = static_cast<int64_t>(static_cast<uint64_t>(new_value) & mask);
        if (signed_type && bits < 64) {
          uint64_t sign_bit = 1ULL << (bits - 1);
          if (static_cast<uint64_t>(new_value) & sign_bit) {
            new_value = static_cast<int64_t>(static_cast<uint64_t>(new_value) | ~((1ULL << bits) - 1));
          }
        }
      }

      if (overflow) {
        results.emplace_back(protocol::RespValue(protocol::RespType::kNullBulkString));
      } else {
        // Set the bits
        uint64_t value_to_set = static_cast<uint64_t>(new_value);
        for (int j = 0; j < bits; ++j) {
          int64_t abs_offset = byte_offset * 8 + bit_offset + j;
          bool bit = (value_to_set >> j) & 1;
          SetBit(bitmap, abs_offset, bit);
        }

        protocol::RespValue resp;
        resp.SetInteger(new_value);
        results.push_back(resp);
      }

      i += 4;
    } else {
      return CommandResult(false, "ERR unknown BITFIELD subcommand");
    }
  }

  // Store result
  db->Set(key, bitmap);

  protocol::RespValue resp;
  resp.SetArray(std::move(results));
  return CommandResult(resp);
}

// BITGET key offset - Get a single bit at offset
CommandResult HandleBitGet(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'bitget' command");
  }

  const std::string& key = command[0].AsString();
  int64_t offset;
  try {
    offset = std::stoll(command[1].AsString());
  } catch (...) {
    return CommandResult(false, "ERR offset is not an integer or out of range");
  }

  if (offset < 0) {
    return CommandResult(false, "ERR offset is out of range");
  }

  auto db = context->GetDatabase();
  auto value = db->Get(key);

  protocol::RespValue resp;
  if (value.has_value()) {
    bool bit = GetBit(value->value, offset);
    resp.SetInteger(bit ? 1 : 0);
  } else {
    resp.SetInteger(0);
  }
  return CommandResult(resp);
}

// BITSET key offset value - Set a single bit at offset
CommandResult HandleBitSet(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 3) {
    return CommandResult(false, "ERR wrong number of arguments for 'bitset' command");
  }

  const std::string& key = command[0].AsString();
  int64_t offset;
  int64_t bit_value;
  try {
    offset = std::stoll(command[1].AsString());
    bit_value = std::stoll(command[2].AsString());
  } catch (...) {
    return CommandResult(false, "ERR offset or value is not an integer or out of range");
  }

  if (offset < 0 || (bit_value != 0 && bit_value != 1)) {
    return CommandResult(false, "ERR offset or value is out of range");
  }

  auto db = context->GetDatabase();
  auto value = db->Get(key);
  std::string bitmap;
  if (value.has_value()) {
    bitmap = value->value;
  }

  SetBit(bitmap, offset, bit_value == 1);
  db->Set(key, bitmap);

  protocol::RespValue resp;
  resp.SetInteger(bit_value);
  return CommandResult(resp);
}

// Auto-register all bitmap commands
ASTRADB_REGISTER_COMMAND(BITCOUNT, -2, "readonly", RoutingStrategy::kByFirstKey, HandleBitCount);
ASTRADB_REGISTER_COMMAND(BITOP, -4, "write", RoutingStrategy::kNone, HandleBitOp);
ASTRADB_REGISTER_COMMAND(BITPOS, -3, "readonly", RoutingStrategy::kByFirstKey, HandleBitPos);
ASTRADB_REGISTER_COMMAND(BITFIELD, -2, "write", RoutingStrategy::kByFirstKey, HandleBitField);
ASTRADB_REGISTER_COMMAND(BITGET, 3, "readonly", RoutingStrategy::kByFirstKey, HandleBitGet);
ASTRADB_REGISTER_COMMAND(BITSET, 4, "write", RoutingStrategy::kByFirstKey, HandleBitSet);

}  // namespace astra::commands