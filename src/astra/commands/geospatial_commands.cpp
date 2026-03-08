// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "geospatial_commands.hpp"
#include "command_auto_register.hpp"
#include "astra/base/logging.hpp"
#include <absl/strings/ascii.h>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace astra::commands {

// ==============================================================================
// Geospatial Implementation
// ==============================================================================
// Geospatial data is stored as a Sorted Set (ZSet) where:
// - Score: Geohash of the coordinates
// - Member: The name of the location

// Earth radius in different units (using quadratic mean radius for WGS-84)
constexpr double kEarthRadiusMeters = 6372797.560856;
constexpr double kEarthRadiusKm = 6372.797560856;
constexpr double kEarthRadiusFeet = 20909579.7;
constexpr double kEarthRadiusMiles = 3958.7600;

// Helper: Convert degrees to radians
static inline double DegToRad(double deg) {
  return deg * M_PI / 180.0;
}

// Helper: Convert radians to degrees (reserved for future use)
[[maybe_unused]] static inline double RadToDeg(double rad) {
  return rad * 180.0 / M_PI;
}

// Interleave lower bits of x and y (Redis implementation)
// x goes to even positions, y goes to odd positions
static inline uint64_t interleave64(uint32_t xlo, uint32_t ylo) {
  static const uint64_t B[] = {0x5555555555555555ULL, 0x3333333333333333ULL,
                               0x0F0F0F0F0F0F0F0FULL, 0x00FF00FF00FF00FFULL,
                               0x0000FFFF0000FFFFULL};
  static const unsigned int S[] = {1, 2, 4, 8, 16};

  uint64_t x = xlo;
  uint64_t y = ylo;

  x = (x | (x << S[4])) & B[4];
  y = (y | (y << S[4])) & B[4];

  x = (x | (x << S[3])) & B[3];
  y = (y | (y << S[3])) & B[3];

  x = (x | (x << S[2])) & B[2];
  y = (y | (y << S[2])) & B[2];

  x = (x | (x << S[1])) & B[1];
  y = (y | (y << S[1])) & B[1];

  x = (x | (x << S[0])) & B[0];
  y = (y | (y << S[0])) & B[0];

  return x | (y << 1);
}

// Reverse the interleave process (Redis implementation)
// Returns deinterleaved value where lower 32 bits are x, upper 32 bits are y
static inline uint64_t deinterleave64(uint64_t interleaved) {
  static const uint64_t B[] = {0x5555555555555555ULL, 0x3333333333333333ULL,
                               0x0F0F0F0F0F0F0F0FULL, 0x00FF00FF00FF00FFULL,
                               0x0000FFFF0000FFFFULL, 0x00000000FFFFFFFFULL};
  static const unsigned int S[] = {0, 1, 2, 4, 8, 16};

  uint64_t x = interleaved;
  uint64_t y = interleaved >> 1;

  x = (x | (x >> S[0])) & B[0];
  y = (y | (y >> S[0])) & B[0];

  x = (x | (x >> S[1])) & B[1];
  y = (y | (y >> S[1])) & B[1];

  x = (x | (x >> S[2])) & B[2];
  y = (y | (y >> S[2])) & B[2];

  x = (x | (x >> S[3])) & B[3];
  y = (y | (y >> S[3])) & B[3];

  x = (x | (x >> S[4])) & B[4];
  y = (y | (y >> S[4])) & B[4];

  x = (x | (x >> S[5])) & B[5];
  y = (y | (y >> S[5])) & B[5];

  return x | (y << 32);
}

// Helper: Calculate geohash (52-bit, similar to Redis)
static uint64_t Geohash52(double longitude, double latitude) {
  // Redis uses a 52-bit geohash (26 bits for longitude, 26 bits for latitude)
  // Range: longitude [-180, 180], latitude [-85.05112878, 85.05112878]
  
  // Normalize coordinates to [0, 1]
  double lon = (longitude + 180.0) / 360.0;
  double lat = (latitude + 85.05112878) / 170.10225756;
  
  // Clamp to valid range
  lon = std::max(0.0, std::min(1.0, lon));
  lat = std::max(0.0, std::min(1.0, lat));
  
  // Convert to 26-bit integer
  uint32_t lon_int = static_cast<uint32_t>(lon * (1ULL << 26));
  uint32_t lat_int = static_cast<uint32_t>(lat * (1ULL << 26));
  
  // Interleave bits: lat in even positions, lon in odd positions
  // Redis calls: interleave64(lat_offset, long_offset)
  return interleave64(lat_int, lon_int);
}

// Helper: Decode geohash to coordinates
static void DecodeGeohash(uint64_t hash, double& longitude, double& latitude) {
  // De-interleave bits
  uint64_t deinterleaved = deinterleave64(hash);
  
  // Extract lat (lower 32 bits) and lon (upper 32 bits)
  uint32_t lat_int = static_cast<uint32_t>(deinterleaved);
  uint32_t lon_int = static_cast<uint32_t>(deinterleaved >> 32);
  
  // Convert from 26-bit integer to [0, 1] range
  double lat = static_cast<double>(lat_int) / (1ULL << 26);
  double lon = static_cast<double>(lon_int) / (1ULL << 26);
  
  // Denormalize to actual coordinates
  latitude = lat * 170.10225756 - 85.05112878;
  longitude = lon * 360.0 - 180.0;
}

// Helper: Calculate Haversine distance
static double HaversineDistance(double lon1, double lat1, double lon2, double lat2, const std::string& unit = "m") {
  double d_lat = DegToRad(lat2 - lat1);
  double d_lon = DegToRad(lon2 - lon1);
  
  lat1 = DegToRad(lat1);
  lat2 = DegToRad(lat2);
  
  double a = std::sin(d_lat / 2) * std::sin(d_lat / 2) +
             std::sin(d_lon / 2) * std::sin(d_lon / 2) * std::cos(lat1) * std::cos(lat2);
  double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
  
  double radius = kEarthRadiusMeters;
  if (unit == "km") {
    radius = kEarthRadiusKm;
  } else if (unit == "ft") {
    radius = kEarthRadiusFeet;
  } else if (unit == "mi") {
    radius = kEarthRadiusMiles;
  }
  
  return radius * c;
}

// Helper: Format distance to fixed-point string (4 decimal places, like Redis)
static std::string FormatDistance(double distance) {
  // Redis uses 4 decimal places for distance output
  char buffer[128];
  snprintf(buffer, sizeof(buffer), "%.4f", distance);
  
  // Remove trailing zeros after decimal point
  std::string result(buffer);
  size_t dot_pos = result.find('.');
  if (dot_pos != std::string::npos) {
    size_t last_non_zero = result.find_last_not_of('0');
    if (last_non_zero != std::string::npos && last_non_zero > dot_pos) {
      result = result.substr(0, last_non_zero + 1);
    }
  }
  
  return result;
}

// Helper: Convert geohash to geohash string (base32)
static std::string GeohashToString(uint64_t hash) {
  static const char base32[] = "0123456789bcdefghjkmnpqrstuvwxyz";
  std::string result;
  
  // Redis uses the first 52 bits (13 characters, each 4 bits)
  for (int i = 0; i < 13; ++i) {
    int shift = 52 - 4 * (i + 1);
    uint8_t index = (hash >> shift) & 0xF;
    result += base32[index];
  }
  
  return result;
}

// GEOADD key [NX|XX] [CH] longitude latitude member [longitude latitude member ...]
CommandResult HandleGeoAdd(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 4 || (command.ArgCount() - 1) % 3 != 0) {
    return CommandResult(false, "ERR wrong number of arguments for 'geoadd' command");
  }

  const std::string& key = command[0].AsString();
  auto db = context->GetDatabase();
  
  size_t idx = 1;
  bool nx = false, xx = false, ch = false;
  
  // Parse options
  while (idx < command.ArgCount()) {
    const std::string& opt = command[idx].AsString();
    if (opt == "NX") {
      nx = true;
      idx++;
    } else if (opt == "XX") {
      xx = true;
      idx++;
    } else if (opt == "CH") {
      ch = true;
      idx++;
    } else {
      break;
    }
  }
  
  if (nx && xx) {
    return CommandResult(false, "ERR NX and XX options at the same time are not compatible");
  }
  
  uint64_t added = 0;
  
  // Process triplets: longitude, latitude, member
  while (idx + 2 < command.ArgCount()) {
    double longitude, latitude;
    
    try {
      if (!absl::SimpleAtod(command[idx].AsString(), &longitude)) {
        return CommandResult(false, "ERR value is not a valid float");
      }
      if (!absl::SimpleAtod(command[idx + 1].AsString(), &latitude)) {
        return CommandResult(false, "ERR value is not a valid float");
      }
    } catch (...) {
      return CommandResult(false, "ERR invalid longitude or latitude");
    }
    
    // Validate coordinates
    if (longitude < -180.0 || longitude > 180.0) {
      return CommandResult(false, "ERR invalid longitude");
    }
    if (latitude < -85.05112878 || latitude > 85.05112878) {
      return CommandResult(false, "ERR invalid latitude");
    }
    
    const std::string& member = command[idx + 2].AsString();
    
    // Calculate geohash
    uint64_t geohash = Geohash52(longitude, latitude);
    
    // Check if member exists
    bool exists = db->ZScore(key, member).has_value();
    
    // Apply NX/XX constraints
    if (nx && exists) {
      idx += 3;
      continue;
    }
    if (xx && !exists) {
      idx += 3;
      continue;
    }
    
    // Add/update member
    db->ZAdd(key, static_cast<double>(geohash), member);
    
    if (!exists || ch) {
      added++;
    }
    
    idx += 3;
  }
  
  protocol::RespValue resp;
  resp.SetInteger(added);
  return CommandResult(resp);
}

// GEODIST key member1 member2 [m|km|ft|mi]
CommandResult HandleGeoDist(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 3 || command.ArgCount() > 4) {
    return CommandResult(false, "ERR wrong number of arguments for 'geodist' command");
  }

  const std::string& key = command[0].AsString();
  const std::string& member1 = command[1].AsString();
  const std::string& member2 = command[2].AsString();
  
  std::string unit = "m";
  if (command.ArgCount() == 4) {
    unit = command[3].AsString();
    if (unit != "m" && unit != "km" && unit != "ft" && unit != "mi") {
      return CommandResult(false, "ERR unknown unit");
    }
  }
  
  auto db = context->GetDatabase();
  
  auto score1 = db->ZScore(key, member1);
  auto score2 = db->ZScore(key, member2);
  
  if (!score1.has_value() || !score2.has_value()) {
    return CommandResult(protocol::RespValue(protocol::RespType::kNullBulkString));
  }
  
  // Decode coordinates
  double lon1, lat1, lon2, lat2;
  DecodeGeohash(static_cast<uint64_t>(*score1), lon1, lat1);
  DecodeGeohash(static_cast<uint64_t>(*score2), lon2, lat2);
  
  // Calculate distance
  double distance = HaversineDistance(lon1, lat1, lon2, lat2, unit);
  
  protocol::RespValue resp;
  resp.SetString(FormatDistance(distance), protocol::RespType::kBulkString);
  return CommandResult(resp);
}

// GEOHASH key member [member ...]
CommandResult HandleGeoHash(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'geohash' command");
  }

  const std::string& key = command[0].AsString();
  auto db = context->GetDatabase();
  
  std::vector<protocol::RespValue> results;
  
  for (size_t i = 1; i < command.ArgCount(); ++i) {
    const std::string& member = command[i].AsString();
    
    auto score = db->ZScore(key, member);
    if (score.has_value()) {
      std::string geohash = GeohashToString(static_cast<uint64_t>(*score));
      results.emplace_back(geohash);
    } else {
      results.emplace_back(protocol::RespValue(protocol::RespType::kNullBulkString));
    }
  }
  
  protocol::RespValue resp;
  resp.SetArray(std::move(results));
  return CommandResult(resp);
}

// GEOPOS key member [member ...]
CommandResult HandleGeoPos(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'geopos' command");
  }

  const std::string& key = command[0].AsString();
  auto db = context->GetDatabase();
  
  std::vector<protocol::RespValue> results;
  
  for (size_t i = 1; i < command.ArgCount(); ++i) {
    const std::string& member = command[i].AsString();
    
    auto score = db->ZScore(key, member);
    if (score.has_value()) {
      double lon, lat;
      DecodeGeohash(static_cast<uint64_t>(*score), lon, lat);
      
      std::vector<protocol::RespValue> pos;
      pos.emplace_back(lon);
      pos.emplace_back(lat);
      results.emplace_back(pos);
    } else {
      results.emplace_back(protocol::RespValue(protocol::RespType::kNullBulkString));
    }
  }
  
  protocol::RespValue resp;
  resp.SetArray(std::move(results));
  return CommandResult(resp);
}

// GEORADIUS key longitude latitude radius m|km|ft|mi [WITHCOORD] [WITHDIST] [WITHHASH] [COUNT count]
CommandResult HandleGeoRadius(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 5) {
    return CommandResult(false, "ERR wrong number of arguments for 'georadius' command");
  }

  const std::string& key = command[0].AsString();
  double longitude, latitude, radius;
  
  try {
    if (!absl::SimpleAtod(command[1].AsString(), &longitude)) {
      return CommandResult(false, "ERR value is not a valid float");
    }
    if (!absl::SimpleAtod(command[2].AsString(), &latitude)) {
      return CommandResult(false, "ERR value is not a valid float");
    }
    if (!absl::SimpleAtod(command[3].AsString(), &radius)) {
      return CommandResult(false, "ERR value is not a valid float");
    }
  } catch (...) {
    return CommandResult(false, "ERR invalid coordinates or radius");
  }
  
  const std::string& unit = command[4].AsString();
  if (unit != "m" && unit != "km" && unit != "ft" && unit != "mi") {
    return CommandResult(false, "ERR unknown unit");
  }
  
  // Validate coordinates
  if (longitude < -180.0 || longitude > 180.0) {
    return CommandResult(false, "ERR invalid longitude");
  }
  if (latitude < -85.05112878 || latitude > 85.05112878) {
    return CommandResult(false, "ERR invalid latitude");
  }
  
  // Parse options
  bool withcoord = false, withdist = false, withhash = false;
  size_t count = 0;
  bool has_count = false;
  
  for (size_t i = 5; i < command.ArgCount(); ++i) {
    const std::string& opt = command[i].AsString();
    if (opt == "WITHCOORD") {
      withcoord = true;
    } else if (opt == "WITHDIST") {
      withdist = true;
    } else if (opt == "WITHHASH") {
      withhash = true;
    } else if (opt == "COUNT") {
      if (i + 1 >= command.ArgCount()) {
        return CommandResult(false, "ERR COUNT option requires a number");
      }
      try {
        count = std::stoull(command[i + 1].AsString());
        has_count = true;
        i++;
      } catch (...) {
        return CommandResult(false, "ERR invalid COUNT value");
      }
    }
  }
  
  auto db = context->GetDatabase();
  
  // Get all members using ZRangeByRank
  auto all_members = db->ZRangeByRank(key, 0, -1, false, true);
  
  if (all_members.empty()) {
    protocol::RespValue resp;
    resp.SetArray({});
    return CommandResult(resp);
  }
  
  // Get all members and calculate distances
  std::vector<std::tuple<std::string, double, double, uint64_t>> candidates;  // member, distance, score, hash
  
  for (const auto& [member, score] : all_members) {
    double lon, lat;
    DecodeGeohash(static_cast<uint64_t>(score), lon, lat);
    double distance = HaversineDistance(longitude, latitude, lon, lat, unit);
    
    if (distance <= radius) {
      candidates.emplace_back(member, distance, score, static_cast<uint64_t>(score));
    }
  }
  
  // Sort by distance
  std::sort(candidates.begin(), candidates.end(),
    [](const auto& a, const auto& b) {
      return std::get<1>(a) < std::get<1>(b);
    });
  
  // Apply COUNT limit
  if (has_count && count < candidates.size()) {
    candidates.resize(count);
  }
  
  // Build result
  std::vector<protocol::RespValue> results;
  for (const auto& [member, distance, score, hash] : candidates) {
    if (!withcoord && !withdist && !withhash) {
      // Simple array of member names
      results.emplace_back(member);
    } else {
      // Array with additional information
      std::vector<protocol::RespValue> item;
      item.emplace_back(member);
      
      if (withdist) {
        item.emplace_back(FormatDistance(distance));
      }
      
      if (withcoord) {
        double lon, lat;
        DecodeGeohash(hash, lon, lat);
        std::vector<protocol::RespValue> coord;
        coord.emplace_back(lon);
        coord.emplace_back(lat);
        item.emplace_back(coord);
      }
      
      if (withhash) {
        item.emplace_back(static_cast<int64_t>(hash));
      }
      
      results.emplace_back(item);
    }
  }
  
  protocol::RespValue resp;
  resp.SetArray(std::move(results));
  return CommandResult(resp);
}

// GEOSEARCH key [MEMBER member] [FROMLONLAT lon lat] [FROMLONLONBYMEMBER member radius unit] [BYRADIUS radius unit] [WITHCOORD] [WITHDIST] [WITHHASH] [COUNT count [ANY]]
CommandResult HandleGeoSearch(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 3) {
    return CommandResult(false, "ERR wrong number of arguments for 'GEOSEARCH' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  if (!key_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of key argument");
  }

  std::string key = key_arg.AsString();

  // Parse search type: BYLONLAT, BYRADIUS, BYBOX, or MEMBER
  std::string search_type = absl::AsciiStrToUpper(command[1].AsString());
  
  double search_lon = 0.0, search_lat = 0.0;
  double radius = 0.0;
  std::string unit = "m";
  bool by_member = false;
  std::string member_name;

  if (search_type == "FROMMEMBER") {
    if (command.ArgCount() < 5) {
      return CommandResult(false, "ERR wrong number of arguments for 'GEOSEARCH' command");
    }
    member_name = command[2].AsString();
    std::string radius_arg = command[3].AsString();
    unit = command[4].AsString();
    
    // Get member coordinates using ZScore and DecodeGeohash
    auto score = db->ZScore(key, member_name);
    if (!score.has_value()) {
      return CommandResult(RespValue(std::vector<RespValue>()));
    }
    DecodeGeohash(static_cast<uint64_t>(*score), search_lon, search_lat);
    by_member = true;

  } else if (search_type == "FROMLONLAT") {
    if (command.ArgCount() < 4) {
      return CommandResult(false, "ERR wrong number of arguments for 'GEOSEARCH' command");
    }
    if (!absl::SimpleAtod(command[2].AsString(), &search_lon) ||
        !absl::SimpleAtod(command[3].AsString(), &search_lat)) {
      return CommandResult(false, "ERR invalid longitude or latitude");
    }

  } else if (search_type == "BYRADIUS") {
    if (command.ArgCount() < 5) {
      return CommandResult(false, "ERR wrong number of arguments for 'GEOSEARCH' command");
    }
    if (!absl::SimpleAtod(command[2].AsString(), &search_lon) ||
        !absl::SimpleAtod(command[3].AsString(), &search_lat) ||
        !absl::SimpleAtod(command[4].AsString(), &radius)) {
      return CommandResult(false, "ERR invalid longitude, latitude or radius");
    }
    if (command.ArgCount() > 5) {
      unit = command[5].AsString();
    }

  } else {
    return CommandResult(false, "ERR unknown GEOSEARCH search type '" + search_type + "'");
  }

  // Parse options
  size_t arg_idx = 5;
  if (by_member) arg_idx = 5;
  if (search_type == "BYRADIUS") arg_idx = 6;

  bool withcoord = false;
  bool withdist = false;
  bool withhash = false;
  int64_t count = -1;

  while (arg_idx < command.ArgCount()) {
    std::string opt = absl::AsciiStrToUpper(command[arg_idx].AsString());
    
    if (opt == "WITHCOORD") {
      withcoord = true;
      arg_idx++;
    } else if (opt == "WITHDIST") {
      withdist = true;
      arg_idx++;
    } else if (opt == "WITHHASH") {
      withhash = true;
      arg_idx++;
    } else if (opt == "COUNT") {
      if (arg_idx + 1 >= command.ArgCount()) {
        return CommandResult(false, "ERR syntax error");
      }
      if (!absl::SimpleAtoi(command[++arg_idx].AsString(), &count)) {
        return CommandResult(false, "ERR value is not an integer or out of range");
      }
      arg_idx++;
    } else {
      return CommandResult(false, "ERR unknown option '" + opt + "'");
    }
  }

  // Get all locations from the sorted set
  auto all_locations = db->ZRange(key, 0, -1, false, true);
  if (all_locations.empty()) {
    return CommandResult(RespValue(std::vector<RespValue>()));
  }

  // Filter locations within radius
  std::vector<std::tuple<std::string, double, double, int64_t>> results;  // member, lon, lat, hash
  
  for (const auto& [member, score] : all_locations) {
    double lon, lat;
    DecodeGeohash(static_cast<int64_t>(score), lon, lat);
    
    double dist = HaversineDistance(search_lon, search_lat, lon, lat, unit);
    
    if (dist <= radius || by_member) {
      results.emplace_back(member, lon, lat, static_cast<int64_t>(score));
    }
  }

  // Sort by distance (for BYRADIUS) or by score (for other types)
  if (search_type == "BYRADIUS" || by_member) {
    std::sort(results.begin(), results.end(), 
      [search_lon, search_lat, unit](const auto& a, const auto& b) {
        double dist_a = HaversineDistance(search_lon, search_lat, 
                                            std::get<1>(a), std::get<2>(a), unit);
        double dist_b = HaversineDistance(search_lon, search_lat, 
                                            std::get<1>(b), std::get<2>(b), unit);
        return dist_a < dist_b;
      });
  }

  // Apply count limit
  if (count > 0 && static_cast<size_t>(count) < results.size()) {
    results.resize(count);
  }

  // Build response
  std::vector<RespValue> response;
  for (const auto& [member, lon, lat, hash] : results) {
    std::vector<RespValue> item;
    
    item.emplace_back(member);
    
    if (withdist) {
      double dist = HaversineDistance(search_lon, search_lat, lon, lat, unit);
      item.emplace_back(dist);
    }
    
    if (withhash) {
      item.emplace_back(hash);
    }
    
    if (withcoord) {
      std::vector<RespValue> coord;
      coord.emplace_back(lon);
      coord.emplace_back(lat);
      item.emplace_back(coord);
    }
    
    response.emplace_back(RespValue(std::move(item)));
  }

  return CommandResult(RespValue(std::move(response)));
}

// GEOSEARCHSTORE destination key [MEMBER member] [FROMLONLAT lon lat] [FROMLONLONBYMEMBER member radius unit] [BYRADIUS radius unit] [WITHCOORD] [WITHDIST] [WITHHASH] [COUNT count [ANY]]
CommandResult HandleGeoSearchStore(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 4) {
    return CommandResult(false, "ERR wrong number of arguments for 'GEOSEARCHSTORE' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& dest_arg = command[0];
  const auto& src_arg = command[1];
  if (!dest_arg.IsBulkString() || !src_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of key argument");
  }

  std::string dest = dest_arg.AsString();
  std::string src = src_arg.AsString();

  // For simplicity, we'll return an error for now
  // A proper implementation would:
  // 1. Execute GEOSEARCH on src key
  // 2. Store results in dest key as a sorted set
  return CommandResult(false, "ERR GEOSEARCHSTORE not fully implemented yet");
}

// GEORADIUS_RO key longitude latitude radius m|km|ft|mi [WITHCOORD] [WITHDIST] [WITHHASH] [COUNT count [ANY]] [ASC|DESC]
CommandResult HandleGeoRadiusRo(const astra::protocol::Command& command, CommandContext* context) {
  // GEORADIUS_RO is a read-only version of GEORADIUS
  // For now, we just call HandleGeoRadius
  return HandleGeoRadius(command, context);
}

// GEORADIUSBYMEMBER key member radius m|km|ft|mi [WITHCOORD] [WITHDIST] [WITHHASH] [COUNT count [ANY]] [ASC|DESC]
CommandResult HandleGeoRadiusByMember(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 4) {
    return CommandResult(false, "ERR wrong number of arguments for 'GEORADIUSBYMEMBER' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  const auto& member_arg = command[1];
  const auto& radius_arg = command[2];
  const auto& unit_arg = command[3];

  if (!key_arg.IsBulkString() || !member_arg.IsBulkString() || 
      !radius_arg.IsBulkString() || !unit_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of argument");
  }

  std::string key = key_arg.AsString();
  std::string member = member_arg.AsString();
  
  double radius;
  if (!absl::SimpleAtod(radius_arg.AsString(), &radius)) {
    return CommandResult(false, "ERR value is not a valid float");
  }
  
  std::string unit = unit_arg.AsString();

  // Get member coordinates using ZScore and DecodeGeohash
  auto score = db->ZScore(key, member);
  if (!score.has_value()) {
    return CommandResult(RespValue(std::vector<RespValue>()));
  }

  double search_lon = 0.0, search_lat = 0.0;
  DecodeGeohash(static_cast<uint64_t>(*score), search_lon, search_lat);

  // Parse options
  size_t arg_idx = 4;
  bool withcoord = false;
  bool withdist = false;
  bool withhash = false;
  int64_t count = -1;
  bool ascending = true;

  while (arg_idx < command.ArgCount()) {
    std::string opt = absl::AsciiStrToUpper(command[arg_idx].AsString());
    
    if (opt == "WITHCOORD") {
      withcoord = true;
      arg_idx++;
    } else if (opt == "WITHDIST") {
      withdist = true;
      arg_idx++;
    } else if (opt == "WITHHASH") {
      withhash = true;
      arg_idx++;
    } else if (opt == "COUNT") {
      if (arg_idx + 1 >= command.ArgCount()) {
        return CommandResult(false, "ERR syntax error");
      }
      if (!absl::SimpleAtoi(command[++arg_idx].AsString(), &count)) {
        return CommandResult(false, "ERR value is not an integer or out of range");
      }
      arg_idx++;
      if (arg_idx < command.ArgCount() && 
          absl::AsciiStrToUpper(command[arg_idx].AsString()) == "ANY") {
        arg_idx++;
      }
    } else if (opt == "ASC") {
      ascending = true;
      arg_idx++;
    } else if (opt == "DESC") {
      ascending = false;
      arg_idx++;
    } else {
      return CommandResult(false, "ERR unknown option '" + opt + "'");
    }
  }

  // Get all locations from the sorted set
  auto all_locations = db->ZRange(key, 0, -1, false, true);
  if (all_locations.empty()) {
    return CommandResult(RespValue(std::vector<RespValue>()));
  }

  // Filter locations within radius
  std::vector<std::tuple<std::string, double, double, int64_t>> results;  // member, lon, lat, hash
  
  for (const auto& [loc_member, score] : all_locations) {
    double lon, lat;
    DecodeGeohash(static_cast<int64_t>(score), lon, lat);
    
    double dist = HaversineDistance(search_lon, search_lat, lon, lat, unit);
    
    if (dist <= radius) {
      results.emplace_back(loc_member, lon, lat, static_cast<int64_t>(score));
    }
  }

  // Sort by distance
  std::sort(results.begin(), results.end(), 
    [search_lon, search_lat, unit, ascending](const auto& a, const auto& b) {
      double dist_a = HaversineDistance(search_lon, search_lat, 
                                          std::get<1>(a), std::get<2>(a), unit);
      double dist_b = HaversineDistance(search_lon, search_lat, 
                                          std::get<1>(b), std::get<2>(b), unit);
      return ascending ? (dist_a < dist_b) : (dist_a > dist_b);
    });

  // Apply count limit
  if (count > 0 && static_cast<size_t>(count) < results.size()) {
    results.resize(count);
  }

  // Build response
  std::vector<RespValue> response;
  for (const auto& [loc_member, lon, lat, hash] : results) {
    std::vector<RespValue> item;
    
    item.emplace_back(loc_member);
    
    if (withdist) {
      double dist = HaversineDistance(search_lon, search_lat, lon, lat, unit);
      item.emplace_back(dist);
    }
    
    if (withhash) {
      item.emplace_back(hash);
    }
    
    if (withcoord) {
      std::vector<RespValue> coord;
      coord.emplace_back(lon);
      coord.emplace_back(lat);
      item.emplace_back(coord);
    }
    
    response.emplace_back(RespValue(std::move(item)));
  }

  return CommandResult(RespValue(std::move(response)));
}

// GEORADIUSBYMEMBER_RO - Read-only version of GEORADIUSBYMEMBER
CommandResult HandleGeoRadiusByMemberRo(const astra::protocol::Command& command, CommandContext* context) {
  // GEORADIUSBYMEMBER_RO is a read-only version of GEORADIUSBYMEMBER
  // For now, we just call HandleGeoRadiusByMember
  return HandleGeoRadiusByMember(command, context);
}

// Auto-register all geospatial commands
ASTRADB_REGISTER_COMMAND(GEOADD, -5, "write", RoutingStrategy::kByFirstKey, HandleGeoAdd);
ASTRADB_REGISTER_COMMAND(GEODIST, -4, "readonly", RoutingStrategy::kByFirstKey, HandleGeoDist);
ASTRADB_REGISTER_COMMAND(GEOHASH, -2, "readonly", RoutingStrategy::kByFirstKey, HandleGeoHash);
ASTRADB_REGISTER_COMMAND(GEOPOS, -2, "readonly", RoutingStrategy::kByFirstKey, HandleGeoPos);
ASTRADB_REGISTER_COMMAND(GEORADIUS, -6, "readonly", RoutingStrategy::kByFirstKey, HandleGeoRadius);
ASTRADB_REGISTER_COMMAND(GEOSEARCH, -3, "readonly", RoutingStrategy::kByFirstKey, HandleGeoSearch);
ASTRADB_REGISTER_COMMAND(GEOSEARCHSTORE, -5, "write", RoutingStrategy::kByFirstKey, HandleGeoSearchStore);
ASTRADB_REGISTER_COMMAND(GEORADIUS_RO, -6, "readonly", RoutingStrategy::kByFirstKey, HandleGeoRadiusRo);
ASTRADB_REGISTER_COMMAND(GEORADIUSBYMEMBER, -4, "readonly", RoutingStrategy::kByFirstKey, HandleGeoRadiusByMember);
ASTRADB_REGISTER_COMMAND(GEORADIUSBYMEMBER_RO, -4, "readonly", RoutingStrategy::kByFirstKey, HandleGeoRadiusByMemberRo);

}  // namespace astra::commands