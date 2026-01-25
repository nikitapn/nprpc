// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "test_swift_bridge.hpp"

// Now we can include the full C++ headers
#include "test_swift_gen.hpp"
#include <nprpc/nprpc.hpp>

namespace Test {

// ============================================================================
// Client handle implementations (opaque wrappers around nprpc::Object)
// ============================================================================

class CalculatorHandle {
public:
  Calculator* client;
  explicit CalculatorHandle(nprpc::Object* obj) 
    : client(dynamic_cast<Calculator*>(obj)) {}
};

class ShapeServiceHandle {
public:
  ShapeService* client;
  explicit ShapeServiceHandle(nprpc::Object* obj)
    : client(dynamic_cast<ShapeService*>(obj)) {}
};

// ============================================================================
// Calculator client bridge
// ============================================================================

extern "C" {

CalculatorHandle* Calculator_create(nprpc::Object* obj) {
  try {
    return new CalculatorHandle(obj);
  } catch (...) {
    return nullptr;
  }
}

void Calculator_destroy(CalculatorHandle* handle) {
  delete handle;
}

void Calculator_add(CalculatorHandle* handle, int32_t a, int32_t b, int32_t* result) {
  if (!handle || !handle->client || !result) return;
  try {
    handle->client->add(a, b, *result);
  } catch (...) {
    // Error handling - could set error flag
  }
}

void Calculator_divide(CalculatorHandle* handle, double numerator, double denominator, double* result) {
  if (!handle || !handle->client || !result) return;
  try {
    handle->client->divide(numerator, denominator, *result);
  } catch (...) {
  }
}

} // extern "C"

// ============================================================================
// ShapeService client bridge
// ============================================================================

extern "C" {

ShapeServiceHandle* ShapeService_create(nprpc::Object* obj) {
  try {
    return new ShapeServiceHandle(obj);
  } catch (...) {
    return nullptr;
  }
}

void ShapeService_destroy(ShapeServiceHandle* handle) {
  delete handle;
}

void ShapeService_getRectangle(ShapeServiceHandle* handle, uint32_t id, Rectangle* rect) {
  if (!handle || !handle->client || !rect) return;
  try {
    handle->client->getRectangle(id, *rect);
  } catch (...) {
  }
}

void ShapeService_setRectangle(ShapeServiceHandle* handle, uint32_t id, const Rectangle* rect) {
  if (!handle || !handle->client || !rect) return;
  try {
    handle->client->setRectangle(id, *rect);
  } catch (...) {
  }
}

} // extern "C"

// ============================================================================
// Swift servant bridges (C++ servant that forwards to Swift)
// ============================================================================

class Calculator_SwiftServantBridge : public ICalculator_Servant {
  CalculatorSwiftServant swift_servant_;
  
public:
  explicit Calculator_SwiftServantBridge(const CalculatorSwiftServant& servant)
    : swift_servant_(servant) {}
  
  void add(int32_t a, int32_t b, int32_t& result) override {
    if (swift_servant_.add_impl) {
      swift_servant_.add_impl(swift_servant_.swift_object, a, b, &result);
    }
  }
  
  void divide(double numerator, double denominator, double& result) override {
    if (swift_servant_.divide_impl) {
      swift_servant_.divide_impl(swift_servant_.swift_object, numerator, denominator, &result);
    }
  }
};

class ShapeService_SwiftServantBridge : public IShapeService_Servant {
  ShapeServiceSwiftServant swift_servant_;
  
public:
  explicit ShapeService_SwiftServantBridge(const ShapeServiceSwiftServant& servant)
    : swift_servant_(servant) {}
  
  void getRectangle(uint32_t id, flat::Rectangle_Direct& rect) override {
    // Convert flat buffer to plain struct for Swift
    Rectangle plain_rect;
    if (swift_servant_.getRectangle_impl) {
      swift_servant_.getRectangle_impl(swift_servant_.swift_object, id, &plain_rect);
      // Copy back to flat buffer
      helper::assign_from_cpp_getRectangle_rect(rect, plain_rect);
    }
  }
  
  void setRectangle(uint32_t id, flat::Rectangle_Direct rect) override {
    // Convert flat buffer to plain struct for Swift
    Rectangle plain_rect;
    helper::assign_from_flat_setRectangle_rect(rect, plain_rect);
    if (swift_servant_.setRectangle_impl) {
      swift_servant_.setRectangle_impl(swift_servant_.swift_object, id, &plain_rect);
    }
  }
};

// ============================================================================
// Servant activation bridge
// ============================================================================

extern "C" {

nprpc::Object* Calculator_activate_servant(
  nprpc::Poa* poa,
  CalculatorSwiftServant* swift_servant
) {
  if (!poa || !swift_servant) return nullptr;
  try {
    auto* servant = new Calculator_SwiftServantBridge(*swift_servant);
    auto oid = poa->activate_object(servant, nprpc::ObjectActivationFlags{});
    return nprpc::create_object_from_servant(poa, oid, servant);
  } catch (...) {
    return nullptr;
  }
}

nprpc::Object* ShapeService_activate_servant(
  nprpc::Poa* poa,
  ShapeServiceSwiftServant* swift_servant
) {
  if (!poa || !swift_servant) return nullptr;
  try {
    auto* servant = new ShapeService_SwiftServantBridge(*swift_servant);
    auto oid = poa->activate_object(servant, nprpc::ObjectActivationFlags{});
    return nprpc::create_object_from_servant(poa, oid, servant);
  } catch (...) {
    return nullptr;
  }
}

} // extern "C"

} // namespace Test
