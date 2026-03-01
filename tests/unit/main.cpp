// ==============================================================================
// Unit Tests Main
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

int main(int argc, char** argv) {
  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
  
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}