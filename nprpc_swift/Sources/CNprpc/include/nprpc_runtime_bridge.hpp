// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// Minimal C++ runtime bridge for Swift-generated marshalling code
// This is the ONLY C++ bridge needed - all marshalling is done in Swift

#ifndef __NPRPC_RUNTIME_BRIDGE_HPP__
#define __NPRPC_RUNTIME_BRIDGE_HPP__

#include <cstdint>
#include <cstddef>

// Forward declarations - no complex C++ headers exposed to Swift
namespace nprpc {
  class Rpc;
  class Poa;
  class Object;
  class ObjectServant;
}

namespace boost::asio {
  class io_context;
}

namespace nprpc_swift {

// ============================================================================
// Flat Buffer - Swift-accessible buffer for marshalling
// ============================================================================

class FlatBufferHandle;

extern "C" {
  // Create/destroy buffers
  FlatBufferHandle* FlatBuffer_create();
  void FlatBuffer_destroy(FlatBufferHandle* buf);
  
  // Buffer operations
  void FlatBuffer_prepare(FlatBufferHandle* buf, size_t size);
  void FlatBuffer_commit(FlatBufferHandle* buf, size_t size);
  void FlatBuffer_consume(FlatBufferHandle* buf, size_t size);
  size_t FlatBuffer_size(FlatBufferHandle* buf);
  
  // Direct memory access for Swift marshalling
  void* FlatBuffer_data(FlatBufferHandle* buf);
  
  // Write primitives (Swift can also write directly via pointer)
  void FlatBuffer_write_u8(FlatBufferHandle* buf, size_t offset, uint8_t value);
  void FlatBuffer_write_u16(FlatBufferHandle* buf, size_t offset, uint16_t value);
  void FlatBuffer_write_u32(FlatBufferHandle* buf, size_t offset, uint32_t value);
  void FlatBuffer_write_u64(FlatBufferHandle* buf, size_t offset, uint64_t value);
  void FlatBuffer_write_i8(FlatBufferHandle* buf, size_t offset, int8_t value);
  void FlatBuffer_write_i16(FlatBufferHandle* buf, size_t offset, int16_t value);
  void FlatBuffer_write_i32(FlatBufferHandle* buf, size_t offset, int32_t value);
  void FlatBuffer_write_i64(FlatBufferHandle* buf, size_t offset, int64_t value);
  void FlatBuffer_write_f32(FlatBufferHandle* buf, size_t offset, float value);
  void FlatBuffer_write_f64(FlatBufferHandle* buf, size_t offset, double value);
  
  // Read primitives
  uint8_t FlatBuffer_read_u8(FlatBufferHandle* buf, size_t offset);
  uint16_t FlatBuffer_read_u16(FlatBufferHandle* buf, size_t offset);
  uint32_t FlatBuffer_read_u32(FlatBufferHandle* buf, size_t offset);
  uint64_t FlatBuffer_read_u64(FlatBufferHandle* buf, size_t offset);
  int8_t FlatBuffer_read_i8(FlatBufferHandle* buf, size_t offset);
  int16_t FlatBuffer_read_i16(FlatBufferHandle* buf, size_t offset);
  int32_t FlatBuffer_read_i32(FlatBufferHandle* buf, size_t offset);
  int64_t FlatBuffer_read_i64(FlatBufferHandle* buf, size_t offset);
  float FlatBuffer_read_f32(FlatBufferHandle* buf, size_t offset);
  double FlatBuffer_read_f64(FlatBufferHandle* buf, size_t offset);
}

// ============================================================================
// Session - Handles RPC calls
// ============================================================================

class SessionHandle;

extern "C" {
  // Get session for an object's endpoint
  SessionHandle* Session_get(nprpc::Object* obj);
  
  // Send/receive (Swift marshals into buf before calling)
  void Session_send_receive(SessionHandle* session, FlatBufferHandle* buf, uint32_t timeout_ms);
  
  // For zero-copy optimization
  bool Session_prepare_zero_copy_buffer(SessionHandle* session, FlatBufferHandle* buf, size_t size);
}

// ============================================================================
// Object & POA - Object lifecycle management
// ============================================================================

extern "C" {
  // Object info (Swift needs these for marshalling call headers)
  uint32_t Object_get_id(nprpc::Object* obj);
  uint8_t Object_get_poa_idx(nprpc::Object* obj);
  uint32_t Object_get_timeout(nprpc::Object* obj);
  void* Object_get_endpoint(nprpc::Object* obj);  // Returns endpoint for session lookup
  
  // POA operations
  nprpc::Object* Poa_activate_servant(
    nprpc::Poa* poa,
    void* swift_servant,      // Opaque Swift servant pointer
    const char* class_name,   // Interface class name
    void (*dispatch_fn)(void* swift_servant, void* session_ctx)  // Swift dispatch function
  );
  
  void Poa_deactivate_object(nprpc::Poa* poa, uint32_t object_id);
}

// ============================================================================
// SessionContext - For servant dispatch (server side)
// ============================================================================

extern "C" {
  // Access buffers during dispatch
  FlatBufferHandle* SessionContext_get_rx_buffer(void* ctx);
  FlatBufferHandle* SessionContext_get_tx_buffer(void* ctx);
  
  // Make simple responses
  void SessionContext_make_simple_answer(void* ctx, uint32_t message_id);
  
  // Zero-copy buffer prep
  bool SessionContext_prepare_zero_copy_buffer(void* ctx, FlatBufferHandle* buf, size_t size);
}

// ============================================================================
// Reply handling
// ============================================================================

extern "C" {
  // Returns -1 if not a standard reply, or MessageId if it is
  int32_t handle_standard_reply(FlatBufferHandle* buf);
}

} // namespace nprpc_swift

#endif // __NPRPC_RUNTIME_BRIDGE_HPP__
