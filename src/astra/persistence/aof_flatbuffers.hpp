// ==============================================================================
// AOF FlatBuffers Serializer (Simplified)
// ==============================================================================
// License: Apache 2.0
// ==============================================================================
// FlatBuffers-based serialization for AOF commands
// Zero-copy, efficient, schema-based
// ==============================================================================

#pragma once

#include <absl/time/clock.h>
#include <absl/time/time.h>

#include <memory>
#include <string>
#include <vector>

#include "aof_generated.h"
#include "astra/base/logging.hpp"
#include "astra/base/version.hpp"

namespace astra::persistence {

// AOF command types (simplified)
enum class AofCommandType {
  kSet = 0,
  kGet = 1,
  kDel = 2,
  kExpire = 3,
  kUnknown = 255
};

// AOF FlatBuffers serializer
class AofFlatbuffersSerializer {
 public:
  // Serialize SET command
  static std::vector<uint8_t> SerializeSetCommand(int db_index,
                                                  const std::string& key,
                                                  const std::string& value) {
    flatbuffers::FlatBufferBuilder builder;

    auto now = absl::ToUnixMillis(absl::Now());

    auto key_offset = builder.CreateString(key);
    auto value_data = builder.CreateVector(
        reinterpret_cast<const uint8_t*>(value.data()), value.size());

    auto string_cmd_offset =
        AstraDB::AOF::CreateStringCommand(builder, key_offset, value_data);

    auto header_offset = AstraDB::AOF::CreateEntryHeader(
        builder, AstraDB::AOF::CommandType_String_Set, db_index, now,
        0,  // sequence
        0   // ttl_ms
    );

    auto entry_offset =
        AstraDB::AOF::CreateAofEntry(builder, header_offset, string_cmd_offset,
                                     0,  // hash_cmd
                                     0,  // list_cmd
                                     0,  // set_cmd
                                     0,  // sortedset_cmd
                                     0,  // bitmap_cmd
                                     0   // hyperloglog_cmd
        );

    builder.Finish(entry_offset);

    return std::vector<uint8_t>(builder.GetBufferPointer(),
                                builder.GetBufferPointer() + builder.GetSize());
  }

  // Serialize GET command
  static std::vector<uint8_t> SerializeGetCommand(int db_index,
                                                  const std::string& key) {
    flatbuffers::FlatBufferBuilder builder;

    auto now = absl::ToUnixMillis(absl::Now());

    auto key_offset = builder.CreateString(key);

    auto string_cmd_offset =
        AstraDB::AOF::CreateStringCommand(builder, key_offset,
                                          0  // value (empty for GET)
        );

    auto header_offset = AstraDB::AOF::CreateEntryHeader(
        builder, AstraDB::AOF::CommandType_String_Set, db_index, now,
        0,  // sequence
        0   // ttl_ms
    );

    auto entry_offset =
        AstraDB::AOF::CreateAofEntry(builder, header_offset, string_cmd_offset,
                                     0,  // hash_cmd
                                     0,  // list_cmd
                                     0,  // set_cmd
                                     0,  // sortedset_cmd
                                     0,  // bitmap_cmd
                                     0   // hyperloglog_cmd
        );

    builder.Finish(entry_offset);

    return std::vector<uint8_t>(builder.GetBufferPointer(),
                                builder.GetBufferPointer() + builder.GetSize());
  }

  // Serialize DEL command
  static std::vector<uint8_t> SerializeDelCommand(int db_index,
                                                  const std::string& key) {
    flatbuffers::FlatBufferBuilder builder;

    auto now = absl::ToUnixMillis(absl::Now());

    auto key_offset = builder.CreateString(key);

    auto string_cmd_offset =
        AstraDB::AOF::CreateStringCommand(builder, key_offset,
                                          0  // value (empty for DEL)
        );

    auto header_offset = AstraDB::AOF::CreateEntryHeader(
        builder, AstraDB::AOF::CommandType_String_Del, db_index, now,
        0,  // sequence
        0   // ttl_ms
    );

    auto entry_offset =
        AstraDB::AOF::CreateAofEntry(builder, header_offset, string_cmd_offset,
                                     0,  // hash_cmd
                                     0,  // list_cmd
                                     0,  // set_cmd
                                     0,  // sortedset_cmd
                                     0,  // bitmap_cmd
                                     0   // hyperloglog_cmd
        );

    builder.Finish(entry_offset);

    return std::vector<uint8_t>(builder.GetBufferPointer(),
                                builder.GetBufferPointer() + builder.GetSize());
  }

  // Serialize EXPIRE command
  static std::vector<uint8_t> SerializeExpireCommand(int db_index,
                                                     const std::string& key,
                                                     int64_t ttl_ms) {
    flatbuffers::FlatBufferBuilder builder;

    auto now = absl::ToUnixMillis(absl::Now());

    auto key_offset = builder.CreateString(key);

    auto string_cmd_offset =
        AstraDB::AOF::CreateStringCommand(builder, key_offset,
                                          0  // value (empty for EXPIRE)
        );

    auto header_offset = AstraDB::AOF::CreateEntryHeader(
        builder, AstraDB::AOF::CommandType_String_Set, db_index, now,
        0,      // sequence
        ttl_ms  // ttl_ms in header
    );

    auto entry_offset =
        AstraDB::AOF::CreateAofEntry(builder, header_offset, string_cmd_offset,
                                     0,  // hash_cmd
                                     0,  // list_cmd
                                     0,  // set_cmd
                                     0,  // sortedset_cmd
                                     0,  // bitmap_cmd
                                     0   // hyperloglog_cmd
        );

    builder.Finish(entry_offset);

    return std::vector<uint8_t>(builder.GetBufferPointer(),
                                builder.GetBufferPointer() + builder.GetSize());
  }

  // Deserialize command (basic implementation)
  static bool DeserializeCommand(const uint8_t* data, size_t size,
                                 AofCommandType& out_type,
                                 std::string& out_key) {
    if (!data || size == 0) {
      return false;
    }

    // Verify minimum size
    if (size < 8) {
      return false;
    }

    try {
      auto entry = AstraDB::AOF::GetAofEntry(data);
      if (!entry) {
        return false;
      }

      auto* header = entry->header();
      if (!header) {
        return false;
      }

      // Map FlatBuffers command type to our enum
      switch (header->command_type()) {
        case AstraDB::AOF::CommandType_String_Set:
          out_type = AofCommandType::kSet;
          break;
        case AstraDB::AOF::CommandType_String_Del:
          out_type = AofCommandType::kDel;
          break;
        default:
          out_type = AofCommandType::kUnknown;
          break;
      }

      // Extract key if available
      if (entry->string_cmd() && entry->string_cmd()->key()) {
        out_key = entry->string_cmd()->key()->str();
      }

      return true;
    } catch (...) {
      return false;
    }
  }
};

}  // namespace astra::persistence
