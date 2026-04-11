// ==============================================================================
// Command Cache FlatBuffers Serializer
// ==============================================================================
// License: Apache 2.0
// ==============================================================================
// FlatBuffers-based serialization for command parameter caching
// Zero-copy, efficient, schema-based
// ==============================================================================

#pragma once

#include <absl/hash/hash.h>
#include <absl/time/clock.h>
#include <absl/time/time.h>

#include <memory>
#include <string>
#include <vector>

#include "generated/command_cache_generated.h"

namespace astra::commands {

// Command parameter value wrapper
struct CachedParameterValue {
  enum class Type {
    kString = 0,
    kInteger = 1,
    kFloat = 2,
    kBoolean = 3,
    kBinary = 4,
    kArray = 5,
    kNull = 255
  };

  Type type;
  std::string string_value;
  int64_t int_value;
  double float_value;
  bool bool_value;
  std::vector<uint8_t> binary_data;
  std::vector<CachedParameterValue> array_values;
  bool is_null;

  CachedParameterValue()
      : type(Type::kNull),
        int_value(0),
        float_value(0.0),
        bool_value(false),
        is_null(true) {}

  explicit CachedParameterValue(const std::string& val)
      : type(Type::kString),
        string_value(val),
        int_value(0),
        float_value(0.0),
        bool_value(false),
        is_null(false) {}

  explicit CachedParameterValue(int64_t val)
      : type(Type::kInteger),
        int_value(val),
        float_value(0.0),
        bool_value(false),
        is_null(false) {}

  explicit CachedParameterValue(double val)
      : type(Type::kFloat),
        int_value(0),
        float_value(val),
        bool_value(false),
        is_null(false) {}

  explicit CachedParameterValue(bool val)
      : type(Type::kBoolean),
        int_value(0),
        float_value(0.0),
        bool_value(val),
        is_null(false) {}

  explicit CachedParameterValue(const std::vector<uint8_t>& val)
      : type(Type::kBinary),
        int_value(0),
        float_value(0.0),
        bool_value(false),
        binary_data(val),
        is_null(false) {}
};

// Command cache entry wrapper
struct CachedCommandEntry {
  std::string command_name;
  std::vector<std::pair<std::string, CachedParameterValue>> parameters;
  std::string hash;
  int64_t created_at_ms;
  int64_t last_accessed_ms;
  uint32_t access_count;
  bool is_read_only;
  uint32_t param_count;
  uint32_t estimated_size_bytes;

  CachedCommandEntry()
      : created_at_ms(0),
        last_accessed_ms(0),
        access_count(0),
        is_read_only(false),
        param_count(0),
        estimated_size_bytes(0) {}
};

// Command cache FlatBuffers serializer
class CommandCacheFlatbuffersSerializer {
 public:
  // Generate hash for command
  static std::string GenerateCommandHash(
      const std::string& command_name,
      const std::vector<std::pair<std::string, CachedParameterValue>>& params) {
    std::string combined = command_name;
    for (const auto& [param_name, param_value] : params) {
      combined += "|" + param_name + ":";
      switch (param_value.type) {
        case CachedParameterValue::Type::kString:
          combined += param_value.string_value;
          break;
        case CachedParameterValue::Type::kInteger:
          combined += std::to_string(param_value.int_value);
          break;
        case CachedParameterValue::Type::kFloat:
          combined += std::to_string(param_value.float_value);
          break;
        case CachedParameterValue::Type::kBoolean:
          combined += param_value.bool_value ? "true" : "false";
          break;
        default:
          combined += "null";
          break;
      }
    }
    return std::to_string(absl::Hash<absl::string_view>{}(combined));
  }

  // Serialize command value
  static flatbuffers::Offset<AstraDB::Commands::CommandValue>
  SerializeCommandValue(flatbuffers::FlatBufferBuilder& builder,
                        const CachedParameterValue& value) {
    flatbuffers::Offset<AstraDB::Commands::CommandValue> result = 0;

    switch (value.type) {
      case CachedParameterValue::Type::kString: {
        auto string_offset = builder.CreateString(value.string_value);
        result = AstraDB::Commands::CreateCommandValue(
            builder, AstraDB::Commands::DataType_String, string_offset,
            0,      // int_value
            0.0,    // float_value
            false,  // bool_value
            0,      // binary_data
            0,      // array_values
            false   // is_null
        );
        break;
      }
      case CachedParameterValue::Type::kInteger: {
        result = AstraDB::Commands::CreateCommandValue(
            builder, AstraDB::Commands::DataType_Integer,
            0,  // string_value
            value.int_value,
            0.0,    // float_value
            false,  // bool_value
            0,      // binary_data
            0,      // array_values
            false   // is_null
        );
        break;
      }
      case CachedParameterValue::Type::kFloat: {
        result = AstraDB::Commands::CreateCommandValue(
            builder, AstraDB::Commands::DataType_Float,
            0,  // string_value
            0,  // int_value
            value.float_value,
            false,  // bool_value
            0,      // binary_data
            0,      // array_values
            false   // is_null
        );
        break;
      }
      case CachedParameterValue::Type::kBoolean: {
        result = AstraDB::Commands::CreateCommandValue(
            builder, AstraDB::Commands::DataType_Boolean,
            0,    // string_value
            0,    // int_value
            0.0,  // float_value
            value.bool_value,
            0,     // binary_data
            0,     // array_values
            false  // is_null
        );
        break;
      }
      case CachedParameterValue::Type::kNull: {
        result = AstraDB::Commands::CreateCommandValue(
            builder, AstraDB::Commands::DataType_Null,
            0,      // string_value
            0,      // int_value
            0.0,    // float_value
            false,  // bool_value
            0,      // binary_data
            0,      // array_values
            true    // is_null
        );
        break;
      }
      default:
        // Default to null
        result = AstraDB::Commands::CreateCommandValue(
            builder, AstraDB::Commands::DataType_Null, 0, 0, 0.0, false, 0, 0,
            true);
        break;
    }

    return result;
  }

  // Serialize command cache entry
  static std::vector<uint8_t> SerializeCacheEntry(
      const CachedCommandEntry& entry) {
    flatbuffers::FlatBufferBuilder builder;

    // Create parameters
    std::vector<flatbuffers::Offset<AstraDB::Commands::CommandParameter>>
        param_offsets;
    for (const auto& [param_name, param_value] : entry.parameters) {
      auto name_offset = builder.CreateString(param_name);
      auto value_offset = SerializeCommandValue(builder, param_value);

      auto param_offset = AstraDB::Commands::CreateCommandParameter(
          builder, name_offset, value_offset,
          -1  // index
      );
      param_offsets.push_back(param_offset);
    }
    auto params_offset = builder.CreateVector(param_offsets);

    // Create entry
    auto name_offset = builder.CreateString(entry.command_name);
    auto hash_offset = builder.CreateString(entry.hash);

    auto entry_offset = AstraDB::Commands::CreateCommandCacheEntry(
        builder, name_offset, params_offset,
        static_cast<uint32_t>(entry.parameters.size()), hash_offset,
        entry.created_at_ms, entry.last_accessed_ms, entry.access_count,
        entry.is_read_only,
        0  // estimated_size_bytes
    );

    builder.Finish(entry_offset);

    return std::vector<uint8_t>(builder.GetBufferPointer(),
                                builder.GetBufferPointer() + builder.GetSize());
  }

  // Serialize cache snapshot
  static std::vector<uint8_t> SerializeCacheSnapshot(
      const std::vector<CachedCommandEntry>& entries, uint64_t hit_count,
      uint64_t miss_count, uint32_t eviction_count) {
    flatbuffers::FlatBufferBuilder builder;

    auto now = absl::ToUnixMillis(absl::Now());

    // Create entries
    std::vector<flatbuffers::Offset<AstraDB::Commands::CommandCacheEntry>>
        entry_offsets;
    for (const auto& entry : entries) {
      // Serialize each entry
      auto name_offset = builder.CreateString(entry.command_name);
      auto hash_offset = builder.CreateString(entry.hash);

      // Serialize parameters
      std::vector<flatbuffers::Offset<AstraDB::Commands::CommandParameter>>
          param_offsets;
      for (const auto& [param_name, param_value] : entry.parameters) {
        auto param_name_offset = builder.CreateString(param_name);

        // Create parameter value
        flatbuffers::Offset<AstraDB::Commands::CommandValue> value_offset;
        switch (param_value.type) {
          case CachedParameterValue::Type::kString: {
            value_offset = AstraDB::Commands::CreateCommandValueDirect(
                builder, AstraDB::Commands::DataType_String,
                param_value.string_value.c_str(), 0, 0.0, false, 0, 0, false);
            break;
          }
          case CachedParameterValue::Type::kInteger:
            value_offset = AstraDB::Commands::CreateCommandValueDirect(
                builder, AstraDB::Commands::DataType_Integer, "",
                param_value.int_value, 0.0, false, 0, 0, false);
            break;
          case CachedParameterValue::Type::kFloat:
            value_offset = AstraDB::Commands::CreateCommandValueDirect(
                builder, AstraDB::Commands::DataType_Float, "", 0,
                param_value.float_value, false, 0, 0, false);
            break;
          case CachedParameterValue::Type::kBoolean:
            value_offset = AstraDB::Commands::CreateCommandValueDirect(
                builder, AstraDB::Commands::DataType_Boolean, "", 0, 0.0,
                param_value.bool_value, 0, 0, false);
            break;
          case CachedParameterValue::Type::kNull:
            value_offset = AstraDB::Commands::CreateCommandValueDirect(
                builder, AstraDB::Commands::DataType_Null, "", 0, 0.0, false, 0,
                0, true);
            break;
          default:
            value_offset = AstraDB::Commands::CreateCommandValueDirect(
                builder, AstraDB::Commands::DataType_Null, "", 0, 0.0, false, 0,
                0, true);
            break;
        }

        auto param_offset = AstraDB::Commands::CreateCommandParameter(
            builder, param_name_offset, value_offset);
        param_offsets.push_back(param_offset);
      }

      auto params_offset = builder.CreateVector(param_offsets);

      // Create entry
      auto entry_offset = AstraDB::Commands::CreateCommandCacheEntry(
          builder, name_offset, params_offset,
          static_cast<uint32_t>(entry.parameters.size()), hash_offset,
          entry.created_at_ms, entry.last_accessed_ms, entry.access_count,
          entry.is_read_only, entry.estimated_size_bytes);
      entry_offsets.push_back(entry_offset);
    }
    auto entries_offset = builder.CreateVector(entry_offsets);

    // Create stats
    auto stats_offset = AstraDB::Commands::CreateCacheStats(
        builder, static_cast<uint32_t>(entries.size()),
        0,  // total_size_bytes (simplified)
        hit_count, miss_count, eviction_count,
        0,  // top_commands
        0   // top_command_counts
    );

    // Create snapshot
    auto version_offset = builder.CreateString("1.0.0");
    auto snapshot_offset = AstraDB::Commands::CreateCacheSnapshot(
        builder, entries_offset, stats_offset, now, version_offset);

    builder.Finish(snapshot_offset);

    return std::vector<uint8_t>(builder.GetBufferPointer(),
                                builder.GetBufferPointer() + builder.GetSize());
  }

  // Deserialize cache entry
  static bool DeserializeCacheEntry(const uint8_t* data, size_t size,
                                    CachedCommandEntry& out_entry) {
    if (!data || size == 0) {
      return false;
    }

    try {
      // The data should contain a CommandCacheEntry table
      auto entry =
          flatbuffers::GetRoot<AstraDB::Commands::CommandCacheEntry>(data);
      if (!entry) {
        return false;
      }

      out_entry.command_name = entry->command_name()->str();
      out_entry.hash = entry->hash()->str();
      out_entry.created_at_ms = entry->created_at_ms();
      out_entry.last_accessed_ms = entry->last_accessed_ms();
      out_entry.access_count = entry->access_count();
      out_entry.is_read_only = entry->is_read_only();
      out_entry.param_count = entry->param_count();
      out_entry.estimated_size_bytes = entry->estimated_size_bytes();

      // Deserialize parameters
      if (entry->parameters()) {
        for (const auto* param : *entry->parameters()) {
          if (!param || !param->value()) continue;

          CachedParameterValue value;
          auto* fb_value = param->value();

          switch (fb_value->data_type()) {
            case AstraDB::Commands::DataType_String:
              value.type = CachedParameterValue::Type::kString;
              value.string_value = fb_value->string_value()->str();
              break;
            case AstraDB::Commands::DataType_Integer:
              value.type = CachedParameterValue::Type::kInteger;
              value.int_value = fb_value->int_value();
              break;
            case AstraDB::Commands::DataType_Float:
              value.type = CachedParameterValue::Type::kFloat;
              value.float_value = fb_value->float_value();
              break;
            case AstraDB::Commands::DataType_Boolean:
              value.type = CachedParameterValue::Type::kBoolean;
              value.bool_value = fb_value->bool_value();
              break;
            case AstraDB::Commands::DataType_Null:
              value.type = CachedParameterValue::Type::kNull;
              break;
            default:
              value.type = CachedParameterValue::Type::kNull;
              break;
          }

          out_entry.parameters.emplace_back(param->name()->str(), value);
        }
      }

      return true;
    } catch (...) {
      return false;
    }
  }

  // Deserialize cache snapshot
  static bool DeserializeCacheSnapshot(
      const uint8_t* data, size_t size,
      std::vector<CachedCommandEntry>& out_entries, uint64_t& out_hit_count,
      uint64_t& out_miss_count, uint32_t& out_eviction_count) {
    if (!data || size == 0) {
      return false;
    }

    try {
      auto snapshot = AstraDB::Commands::GetCacheSnapshot(data);
      if (!snapshot) {
        return false;
      }

      // Get stats
      if (snapshot->stats()) {
        out_hit_count = snapshot->stats()->hit_count();
        out_miss_count = snapshot->stats()->miss_count();
        out_eviction_count = snapshot->stats()->eviction_count();
      }

      // Deserialize entries
      if (snapshot->entries()) {
        for (const auto* fb_entry : *snapshot->entries()) {
          if (!fb_entry) continue;

          CachedCommandEntry entry;
          entry.command_name = fb_entry->command_name()->str();
          entry.hash = fb_entry->hash()->str();
          entry.created_at_ms = fb_entry->created_at_ms();
          entry.last_accessed_ms = fb_entry->last_accessed_ms();
          entry.access_count = fb_entry->access_count();
          entry.is_read_only = fb_entry->is_read_only();
          entry.param_count = fb_entry->param_count();
          entry.estimated_size_bytes = fb_entry->estimated_size_bytes();

          // Deserialize parameters
          if (fb_entry->parameters()) {
            for (const auto* param : *fb_entry->parameters()) {
              if (!param || !param->value()) continue;

              CachedParameterValue value;
              auto* fb_value = param->value();

              switch (fb_value->data_type()) {
                case AstraDB::Commands::DataType_String:
                  value.type = CachedParameterValue::Type::kString;
                  value.string_value = fb_value->string_value()->str();
                  break;
                case AstraDB::Commands::DataType_Integer:
                  value.type = CachedParameterValue::Type::kInteger;
                  value.int_value = fb_value->int_value();
                  break;
                case AstraDB::Commands::DataType_Float:
                  value.type = CachedParameterValue::Type::kFloat;
                  value.float_value = fb_value->float_value();
                  break;
                case AstraDB::Commands::DataType_Boolean:
                  value.type = CachedParameterValue::Type::kBoolean;
                  value.bool_value = fb_value->bool_value();
                  break;
                case AstraDB::Commands::DataType_Null:
                  value.type = CachedParameterValue::Type::kNull;
                  break;
                default:
                  value.type = CachedParameterValue::Type::kNull;
                  break;
              }

              entry.parameters.emplace_back(param->name()->str(), value);
            }
          }

          out_entries.push_back(std::move(entry));
        }
      }

      return true;
    } catch (...) {
      return false;
    }
  }
};

}  // namespace astra::commands
