// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "command_handler.hpp"
#include "database.hpp"

namespace astra::commands {

// Geospatial Operations
// Stores geographic coordinates (longitude, latitude) with member names

// GEOADD key [NX|XX] [CH] longitude latitude member [longitude latitude member
// ...] Adds geospatial items to the specified key
CommandResult HandleGeoAdd(const protocol::Command& command,
                           CommandContext* context);

// GEODIST key member1 member2 [m|km|ft|mi]
// Returns the distance between two members in the geospatial index
CommandResult HandleGeoDist(const protocol::Command& command,
                            CommandContext* context);

// GEOHASH key member [member ...]
// Returns valid Geohash strings representing the position of the elements
CommandResult HandleGeoHash(const protocol::Command& command,
                            CommandContext* context);

// GEOPOS key member [member ...]
// Returns the positions (longitude,latitude) of all the specified members
CommandResult HandleGeoPos(const protocol::Command& command,
                           CommandContext* context);

// GEORADIUS key longitude latitude radius m|km|ft|mi [WITHCOORD] [WITHDIST]
// [WITHHASH] [COUNT count] Query a sorted set representing a geospatial index
// to find members inside a given area
CommandResult HandleGeoRadius(const protocol::Command& command,
                              CommandContext* context);

}  // namespace astra::commands
