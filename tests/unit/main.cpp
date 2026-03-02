// ==============================================================================
// Unit Tests Main
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include "astra/base/logging.hpp"

int main(int argc, char** argv) {
  astra::base::InitLogging();
  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}