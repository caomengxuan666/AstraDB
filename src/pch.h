// ==============================================================================
// Precompiled Header (PCH) for AstraDB
// ==============================================================================
// This header is precompiled to accelerate compilation times.
// It contains frequently used standard and third-party library headers.
// ==============================================================================

#pragma once

// C library headers
#include <stdio.h>
#include <stdlib.h>

// C++ Standard Library - Most Common Headers (>100 times used)
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <ostream>
#include <queue>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <filesystem>
#include <fstream>


// Abseil Library - Core Components
#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <absl/container/inlined_vector.h>
#include <absl/functional/any_invocable.h>
#include <absl/hash/hash.h>
#include <absl/strings/ascii.h>
#include <absl/strings/match.h>
#include <absl/strings/numbers.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/str_split.h>
#include <absl/strings/string_view.h>
#include <absl/synchronization/mutex.h>
#include <absl/time/time.h>
#include <absl/types/span.h>

// ASIO (Header-only)
#include <asio.hpp>

// SPDLOG
#include <spdlog/spdlog.h>
#include <spdlog/common.h>                
#include <spdlog/details/null_mutex.h>       
#include <spdlog/details/os.h>                  
#include <spdlog/sinks/base_sink.h>             
#include <spdlog/details/synchronous_factory.h> 
#include <spdlog/details/log_msg.h>              
#include <spdlog/fmt/ostr.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

// fmt
#include <fmt/format.h>
#include <fmt/ostream.h>

// JSON
#include <nlohmann/json.hpp>

// FlatBuffers
#include <flatbuffers/flatbuffers.h>

// ==============================================================================
// Project Headers - Core Infrastructure
// ==============================================================================
#include "astra/base/logging.hpp"
#include "astra/base/macros.hpp"
#include "astra/base/simd_utils.hpp"
#include "astra/base/concurrentqueue_wrapper.hpp"

#include "astra/commands/database.hpp"
#include "astra/commands/command_handler.hpp"
#include "astra/protocol/resp/resp_types.hpp"
#include "astra/protocol/resp/resp_builder.hpp"

// ==============================================================================
// Platform-specific includes
// ==============================================================================
#if defined(__linux__) && defined(ASIO_HAS_IO_URING)
// io_uring is Linux-only
#include <liburing.h>
#endif
