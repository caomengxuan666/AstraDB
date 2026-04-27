// ==============================================================================
// Vector Search Commands — AstraDB-native vector search API
// ==============================================================================
// License: Apache 2.0
// Architecture: NO SHARING — single-key commands hash-slot routed,
//   VSEARCH broadcasts via CrossWorkerRequest scatter-gather
// ==============================================================================

#pragma once

#include "../command_handler.hpp"

namespace astra::commands {

// Vector commands are auto-registered via ASTRADB_REGISTER_COMMAND macro

}  // namespace astra::commands
