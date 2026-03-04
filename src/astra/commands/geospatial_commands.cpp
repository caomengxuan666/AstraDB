// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "geospatial_commands.hpp"
#include "command_auto_register.hpp"
#include "astra/base/logging.hpp"
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

// Earth radius in different units
constexpr double kEarthRadiusMeters = 6378137.0;
constexpr double kEarthRadiusKm = 6378.137;
constexpr double kEarthRadiusFeet = 20908792.0;
constexpr double kEarthRadiusMiles = 3958.761;

// Helper: Convert degrees to radians
static inline double DegToRad(double deg) {
  return deg * M_PI / 180.0;
}

// Helper: Convert radians to degrees
static inline double RadToDeg(double rad) {
  return rad * 180.0 / M_PI;
}

// Helper: Calculate geohash (52-bit, similar to Redis)
static uint64_t Geohash52(double longitude, double latitude) {
  // Redis uses a 52-bit geohash (26 bits for longitude, 26 bits for latitude)
  // Range: longitude [-180, 180], latitude [-85.05112878, 85.05112878]
  
  // Normalize coordinates
  double lon = (longitude + 180.0) / 360.0;  // [0, 1]
  double lat = (latitude + 85.05112878) / 170.10225756;  // [0, 1]
  
  // Clamp to valid range
  lon = std::max(0.0, std::min(1.0, lon));
  lat = std::max(0.0, std::min(1.0, lat));
  
  // Interleave bits to create 52-bit geohash
  uint64_t hash = 0;
  for (int i = 0; i < 26; ++i) {
    // Get bit i from latitude and longitude
    uint64_t lat_bit = static_cast<uint64_t>(lat * (1ULL << 26)) & (1ULL << (25 - i));
    uint64_t lon_bit = static_cast<uint64_t>(lon * (1ULL << 26)) & (1ULL << (25 - i));
    
    // Interleave: latitude bits in odd positions, longitude in even
    hash |= (lat_bit << (2 * i + 1));
    hash |= (lon_bit << (2 * i));
  }
  
  return hash;
}

// Helper: Decode geohash to coordinates
static void DecodeGeohash(uint64_t hash, double& longitude, double& latitude) {
  double lon = 0.0, lat = 0.0;
  
  for (int i = 0; i < 26; ++i) {
    // Extract bits
    uint64_t lat_bit = (hash >> (2 * i + 1)) & 1;
    uint64_t lon_bit = (hash >> (2 * i)) & 1;
    
    // Reconstruct coordinates
    lat += lat_bit * (1.0 / (1ULL << (i + 1)));
    lon += lon_bit * (1.0 / (1ULL << (i + 1)));
  }
  
  // Denormalize
  lon = lon * 360.0 - 180.0;
  lat = lat * 170.10225756 - 85.05112878;
  
  longitude = lon;
  latitude = lat;
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
      longitude = std::stod(command[idx].AsString());
      latitude = std::stod(command[idx + 1].AsString());
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
  resp.SetString(std::to_string(distance), protocol::RespType::kBulkString);
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
    longitude = std::stod(command[1].AsString());
    latitude = std::stod(command[2].AsString());
    radius = std::stod(command[3].AsString());
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
  
  // Get all members using ZRange
  auto all_members = db->ZRange(key, 0, -1, true);
  
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
        item.emplace_back(distance);
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

// Auto-register all geospatial commands
ASTRADB_REGISTER_COMMAND(GEOADD, -5, "write", RoutingStrategy::kByFirstKey, HandleGeoAdd);
ASTRADB_REGISTER_COMMAND(GEODIST, -4, "readonly", RoutingStrategy::kByFirstKey, HandleGeoDist);
ASTRADB_REGISTER_COMMAND(GEOHASH, -2, "readonly", RoutingStrategy::kByFirstKey, HandleGeoHash);
ASTRADB_REGISTER_COMMAND(GEOPOS, -2, "readonly", RoutingStrategy::kByFirstKey, HandleGeoPos);
ASTRADB_REGISTER_COMMAND(GEORADIUS, -6, "readonly", RoutingStrategy::kByFirstKey, HandleGeoRadius);

}  // namespace astra::commands