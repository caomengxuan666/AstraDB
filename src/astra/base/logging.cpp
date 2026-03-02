// ==============================================================================
// Logging Implementation
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include <filesystem>
#include <iostream>

#include "astra/base/logging.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/async.h>

namespace astra::base {

void InitLogging(const std::string& log_file, spdlog::level::level_enum level, 
               bool async, size_t queue_size) {
  try {
    std::vector<spdlog::sink_ptr> sinks;
    
    // Create console sink
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(level);
    console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
    sinks.push_back(console_sink);
    
    // Create file sink if log_file is specified
    if (!log_file.empty()) {
      // Create directory if it doesn't exist
      std::filesystem::path log_path(log_file);
      if (log_path.has_parent_path()) {
        std::filesystem::create_directories(log_path.parent_path());
      }
      
      auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
          log_file, 1024 * 1024 * 100, 3);  // 100MB, 3 files
      file_sink->set_level(level);
      file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v");
      sinks.push_back(file_sink);
    }
    
    if (async) {
      // Initialize global thread pool for async logging
      spdlog::init_thread_pool(queue_size, 1);
      
      // Create async logger
      g_logger = std::make_shared<spdlog::async_logger>(
          "astradb",
          sinks.begin(),
          sinks.end(),
          spdlog::thread_pool(),
          spdlog::async_overflow_policy::block);
    } else {
      // Use synchronous logger
      g_logger = std::make_shared<spdlog::logger>(
          "astradb", sinks.begin(), sinks.end());
    }
    
    g_logger->set_level(level);
    g_logger->flush_on(spdlog::level::warn);
    
    // Set as default logger
    spdlog::set_default_logger(g_logger);
    
  } catch (const spdlog::spdlog_ex& ex) {
    std::cerr << "Log initialization failed: " << ex.what() << std::endl;
  }
}

} // namespace astra::base