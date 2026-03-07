// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "command_handler.hpp"
#include "database.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace astra::commands {

// BITMAP Operations
// Bitmap is stored as a string, where each character is a byte (8 bits)

// BITCOUNT key [start end] - Count set bits in a string
CommandResult HandleBitCount(const protocol::Command& command, CommandContext* context);

// BITOP operation destkey key [key ...] - Perform bitwise operations between strings
CommandResult HandleBitOp(const protocol::Command& command, CommandContext* context);

// BITPOS key bit [start end] - Find first bit set or clear in a string
CommandResult HandleBitPos(const protocol::Command& command, CommandContext* context);

// BITFIELD key [GET encoding offset] [SET encoding offset value] [INCRBY encoding offset increment] [OVERFLOW WRAP|SAT|FAIL] - Perform bitfield operations
CommandResult HandleBitField(const protocol::Command& command, CommandContext* context);

// GETBIT key offset - Get a single bit at offset
CommandResult HandleGetBit(const protocol::Command& command, CommandContext* context);

// SETBIT key offset value - Set a single bit at offset
CommandResult HandleSetBit(const protocol::Command& command, CommandContext* context);

}  // namespace astra::commands