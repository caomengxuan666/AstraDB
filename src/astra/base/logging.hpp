// ==============================================================================
// Logging Interface
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <spdlog/spdlog.h>

#include <string>

namespace astra::base {

// Global logger instance
inline std::shared_ptr<spdlog::logger> g_logger;

// Initialize logging system
void InitLogging(const std::string& log_file = "",
                 spdlog::level::level_enum level = spdlog::level::info,
                 bool async = true, size_t queue_size = 8192);

// Log macros
// TRACE: Developer debugging, can be compiled out in release builds
// DEBUG: Problem diagnosis, runtime controllable
// INFO and above: Important events users care about
#ifdef ASTRADB_DISABLE_TRACE
  #define ASTRADB_LOG_TRACE(...) (void)0  // Compile-out TRACE logs
#else
  #define ASTRADB_LOG_TRACE(...) ::astra::base::g_logger->trace(__VA_ARGS__)
#endif

#define ASTRADB_LOG_DEBUG(...) ::astra::base::g_logger->debug(__VA_ARGS__)
#define ASTRADB_LOG_INFO(...) ::astra::base::g_logger->info(__VA_ARGS__)
#define ASTRADB_LOG_WARN(...) ::astra::base::g_logger->warn(__VA_ARGS__)
#define ASTRADB_LOG_ERROR(...) ::astra::base::g_logger->error(__VA_ARGS__)
#define ASTRADB_LOG_CRITICAL(...) ::astra::base::g_logger->critical(__VA_ARGS__)

}  // namespace astra::base
