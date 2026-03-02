// ==============================================================================
// Data Type Registry - Compile-time type-erased data structure management
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <memory>
#include <string>
#include <functional>
#include "astra/base/constexpr_map.hpp"

namespace astra::commands {

// Forward declarations
class Database;

// Data type identifier
enum class DataType : uint8_t {
  kNone = 0,
  kString = 1,
  kHash = 2,
  kSet = 3,
  kZSet = 4,
  kList = 5,
  kStream = 6
};

// Type-erased container interface
class IContainer {
 public:
  virtual ~IContainer() = default;
  virtual DataType GetType() const = 0;
  virtual bool IsEmpty() const = 0;
  virtual size_t Size() const = 0;
  virtual void Clear() = 0;
  virtual size_t MemoryUsage() const = 0;
};

// Type-erased container factory
using ContainerFactory = std::function<std::unique_ptr<IContainer>()>;

// Compile-time data type registration
template <DataType... Types>
class DataTypeRegistry {
 public:
  using FactoryMap = astra::base::ConstexprMap<
      DataType,
      ContainerFactory,
      sizeof...(Types)
  >;

  // Singleton instance
  static DataTypeRegistry& Instance() {
    static DataTypeRegistry instance;
    return instance;
  }

  // Get factory for a data type (compile-time)
  constexpr const ContainerFactory* GetFactory(DataType type) const {
    return factories_.try_get(type);
  }

  // Create a container for a data type (runtime)
  std::unique_ptr<IContainer> Create(DataType type) const {
    const auto* factory = GetFactory(type);
    if (factory) {
      return (*factory)();
    }
    return nullptr;
  }

  // Check if a type is registered (compile-time)
  constexpr bool IsRegistered(DataType type) const {
    return factories_.contains(type);
  }

 private:
  constexpr DataTypeRegistry(const FactoryMap& factories)
      : factories_(factories) {}

  FactoryMap factories_;
};

// Helper to build factory map at compile time
template <DataType... Types>
constexpr auto make_factory_map(
    std::array<std::pair<DataType, ContainerFactory>, sizeof...(Types)> factories) {
  return astra::base::make_constexpr_map(factories);
}

// RAII helper for automatic registration (runtime)
class DataTypeRegistrar {
 public:
  DataTypeRegistrar(DataType type, ContainerFactory factory);
};

// Global storage for runtime registration (for dynamic loading)
struct RuntimeDataTypeRegistry {
  static RuntimeDataTypeRegistry& Instance() {
    static RuntimeDataTypeRegistry instance;
    return instance;
  }

  void Register(DataType type, ContainerFactory factory) {
    factories_[static_cast<int>(type)] = std::move(factory);
  }

  std::unique_ptr<IContainer> Create(DataType type) const {
    auto idx = static_cast<int>(type);
    if (idx >= 0 && idx < 8 && factories_[idx]) {
      return factories_[idx]();
    }
    return nullptr;
  }

 private:
  std::array<ContainerFactory, 8> factories_{nullptr};
};

// Macro for automatic registration (compile-time if possible)
#define ASTRADB_REGISTER_DATA_TYPE(Type, FactoryClass) \
  namespace { \
    DataTypeRegistrar g_##Type##_registrar(DataType::k##Type, []() { \
      return std::make_unique<FactoryClass>(); \
    }); \
  }

}  // namespace astra::commands