// ==============================================================================
// AstraDB Main Entry Point
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include "astra/astra.hpp"
#include "astra/base/logging.hpp"
#include "astra/base/macros.hpp"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
  // Initialize logging
  astra::base::InitLogging();
  
  ASTRADB_LOG_INFO("========================================");
  ASTRADB_LOG_INFO("AstraDB - High-Performance Redis-Compatible Database");
  ASTRADB_LOG_INFO("Version: {}", ASTRADB_VERSION_STRING);
  ASTRADB_LOG_INFO("========================================");
  
  // Print build information
  ASTRADB_LOG_INFO("Build Configuration:");
  ASTRADB_LOG_INFO("  Platform: {}", 
#if defined(ASTRADB_PLATFORM_WINDOWS)
      "Windows"
#elif defined(ASTRADB_PLATFORM_MACOS)
      "macOS"
#elif defined(ASTRADB_PLATFORM_LINUX)
      "Linux"
#else
      "Unknown"
#endif
  );
  
  ASTRADB_LOG_INFO("  Architecture: {}",
#if defined(ASTRADB_ARCH_X64)
      "x86_64"
#elif defined(ASTRADB_ARCH_ARM64)
      "ARM64"
#else
      "Unknown"
#endif
  );
  
  ASTRADB_LOG_INFO("  Compiler: {}",
#if defined(ASTRADB_COMPILER_CLANG)
      "Clang"
#elif defined(ASTRADB_COMPILER_GCC)
      "GCC"
#elif defined(ASTRADB_COMPILER_MSVC)
      "MSVC"
#else
      "Unknown"
#endif
  );
  
  ASTRADB_LOG_INFO("  C++ Standard: {}", __cplusplus);
  
  // Print features
  ASTRADB_LOG_INFO("\nEnabled Features:");
#if defined(ASTRADB_ENABLE_TLS)
  ASTRADB_LOG_INFO("  [+] TLS Encryption");
#endif
#if defined(ASTRADB_ENABLE_ACL)
  ASTRADB_LOG_INFO("  [+] Access Control List (ACL)");
#endif
#if defined(ASTRADB_ENABLE_SIMD)
  ASTRADB_LOG_INFO("  [+] SIMD Optimizations");
#endif
  
  ASTRADB_LOG_INFO("\n========================================");
  ASTRADB_LOG_INFO("AstraDB initialized successfully!");
  ASTRADB_LOG_INFO("========================================");
  
  std::cout << "\n🚀 AstraDB is ready!\n\n";
  
  return 0;
}