// ==============================================================================
// Data Type Registry Implementation
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include "data_type_registry.hpp"

namespace astra::commands {

DataTypeRegistrar::DataTypeRegistrar(DataType type, ContainerFactory factory) {
  RuntimeDataTypeRegistry::Instance().Register(type, std::move(factory));
}

}  // namespace astra::commands
