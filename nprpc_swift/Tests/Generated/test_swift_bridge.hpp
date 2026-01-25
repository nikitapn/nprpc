// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// Swift bridge for generated test_swift_gen stubs
// This header is safe to import from Swift - no complex C++ includes

#ifndef __TEST_SWIFT_BRIDGE_HPP__
#define __TEST_SWIFT_BRIDGE_HPP__

#include <cstdint>

// Forward declarations - no nprpc includes in header
namespace nprpc {
  class Poa;
  class Object;
  class ObjectServant;
  class SessionContext;
}

namespace Test {

// Plain enums and structs are safe for Swift
enum class Color : uint32_t {
  Red = 0,
  Green = 1,
  Blue = 2
};

struct Point {
  int32_t x;
  int32_t y;
};

struct Rectangle {
  Point topLeft;
  Point bottomRight;
  Color color;
};

// Opaque handle to Calculator client (wraps nprpc::Object)
class CalculatorHandle;

// Opaque handle to ShapeService client (wraps nprpc::Object)
class ShapeServiceHandle;

// Bridge API for Calculator client (called from Swift)
extern "C" {
  // Create client from object reference
  CalculatorHandle* Calculator_create(nprpc::Object* obj);
  void Calculator_destroy(CalculatorHandle* handle);
  
  // Method calls
  void Calculator_add(CalculatorHandle* handle, int32_t a, int32_t b, int32_t* result);
  void Calculator_divide(CalculatorHandle* handle, double numerator, double denominator, double* result);
}

// Bridge API for ShapeService client
extern "C" {
  ShapeServiceHandle* ShapeService_create(nprpc::Object* obj);
  void ShapeService_destroy(ShapeServiceHandle* handle);
  
  void ShapeService_getRectangle(ShapeServiceHandle* handle, uint32_t id, Rectangle* rect);
  void ShapeService_setRectangle(ShapeServiceHandle* handle, uint32_t id, const Rectangle* rect);
}

// Swift servant protocol - implemented in Swift, bridged to C++
// The Swift implementation provides a pointer to a Swift object + function pointers
struct CalculatorSwiftServant {
  void* swift_object;  // Opaque pointer to Swift servant instance
  
  // Function pointers to Swift implementations
  void (*add_impl)(void* swift_object, int32_t a, int32_t b, int32_t* result);
  void (*divide_impl)(void* swift_object, double numerator, double denominator, double* result);
};

struct ShapeServiceSwiftServant {
  void* swift_object;
  
  void (*getRectangle_impl)(void* swift_object, uint32_t id, Rectangle* rect);
  void (*setRectangle_impl)(void* swift_object, uint32_t id, const Rectangle* rect);
};

// Bridge API for activating Swift servants
extern "C" {
  // Activate Calculator servant (returns object ID for client creation)
  nprpc::Object* Calculator_activate_servant(
    nprpc::Poa* poa,
    CalculatorSwiftServant* swift_servant
  );
  
  nprpc::Object* ShapeService_activate_servant(
    nprpc::Poa* poa,
    ShapeServiceSwiftServant* swift_servant
  );
}

} // namespace Test

#endif // __TEST_SWIFT_BRIDGE_HPP__
