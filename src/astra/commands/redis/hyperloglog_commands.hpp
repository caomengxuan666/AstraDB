// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "command_handler.hpp"
#include "database.hpp"

namespace astra::commands {

// HyperLogLog Operations
// HyperLogLog is a probabilistic data structure for estimating the cardinality
// of large datasets

// PFADD key [element ...] - Adds elements to a HyperLogLog key
CommandResult HandlePfAdd(const protocol::Command& command,
                          CommandContext* context);

// PFCOUNT key [key ...] - Returns the approximated cardinality of the set(s)
// observed by the HyperLogLog
CommandResult HandlePfCount(const protocol::Command& command,
                            CommandContext* context);

// PFMERGE destkey sourcekey [sourcekey ...] - Merges multiple HyperLogLog
// values into a single value
CommandResult HandlePfMerge(const protocol::Command& command,
                            CommandContext* context);

}  // namespace astra::commands
