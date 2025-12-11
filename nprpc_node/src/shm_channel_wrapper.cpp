// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// nprpc_node - Native Node.js addon for shared memory transport

#include "shm_channel_wrapper.hpp"
#include "converter.hpp"
#include "nprpc_node.hpp"
#include <nprpc/impl/lock_free_ring_buffer.hpp>

#include <cstring>
#include <fcntl.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nprpc_node {

using LockFreeRingBuffer = nprpc::impl::LockFreeRingBuffer;

//==============================================================================
// ShmChannel Implementation
//==============================================================================

ShmChannel::ShmChannel(const std::string& channel_id,
                       bool is_server,
                       bool create)
    : channel_id_(channel_id)
    , is_server_(is_server)
{
  // Ring buffer names follow the same convention as C++ server
  // Server writes to "s2c", client writes to "c2s"
  if (is_server) {
    send_ring_name_ = "/nprpc_" + channel_id + "_s2c";
    recv_ring_name_ = "/nprpc_" + channel_id + "_c2s";
  } else {
    send_ring_name_ = "/nprpc_" + channel_id + "_c2s";
    recv_ring_name_ = "/nprpc_" + channel_id + "_s2c";
  }

  try {
    if (create) {
      // Create new ring buffers using factory methods
      send_ring_ =
          LockFreeRingBuffer::create(send_ring_name_, RING_BUFFER_SIZE);
      recv_ring_ =
          LockFreeRingBuffer::create(recv_ring_name_, RING_BUFFER_SIZE);
    } else {
      // Open existing ring buffers
      send_ring_ = LockFreeRingBuffer::open(send_ring_name_);
      recv_ring_ = LockFreeRingBuffer::open(recv_ring_name_);
    }

    // Create eventfd for libuv polling (used for signaling data
    // availability)
    eventfd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (eventfd_ < 0) {
      error_ = "Failed to create eventfd: " + std::string(strerror(errno));
      send_ring_.reset();
      recv_ring_.reset();
      return;
    }

  } catch (const std::exception& e) {
    error_ = e.what();
    send_ring_.reset();
    recv_ring_.reset();
  }
}

ShmChannel::~ShmChannel()
{
  if (eventfd_ >= 0) {
    close(eventfd_);
  }

  // Ring buffers clean up via RAII destructors
  // If is_creator_ is set inside the ring buffer, it will unlink the shm
}

bool ShmChannel::send(const uint8_t* data, uint32_t size)
{
  if (!send_ring_ || size > MAX_MESSAGE_SIZE) {
    return false;
  }

  return send_ring_->try_write(data, size);
}

int32_t ShmChannel::try_receive(uint8_t* buffer, size_t buffer_size)
{
  if (!recv_ring_) {
    return -1;
  }

  size_t bytes_read = recv_ring_->try_read(buffer, buffer_size);
  return static_cast<int32_t>(bytes_read);
}

bool ShmChannel::has_data() const
{
  if (!recv_ring_) {
    return false;
  }
  return !recv_ring_->is_empty();
}

//==============================================================================
// ShmChannelWrapper (N-API) Implementation
//==============================================================================

Napi::Object ShmChannelWrapper::Init(Napi::Env env, Napi::Object exports)
{
  Napi::Function func = DefineClass(
      env, "ShmChannel",
      {
          InstanceMethod("isOpen", &ShmChannelWrapper::IsOpen),
          InstanceMethod("getChannelId", &ShmChannelWrapper::GetChannelId),
          InstanceMethod("getError", &ShmChannelWrapper::GetError),
          InstanceMethod("send", &ShmChannelWrapper::Send),
          InstanceMethod("tryReceive", &ShmChannelWrapper::TryReceive),
          InstanceMethod("tryReceiveSSRRequest",
                         &ShmChannelWrapper::TryReceiveSSRRequest),
          InstanceMethod("sendSSRResponse",
                         &ShmChannelWrapper::SendSSRResponse),
          InstanceMethod("hasData", &ShmChannelWrapper::HasData),
          InstanceMethod("close", &ShmChannelWrapper::Close),
          InstanceMethod("startPolling", &ShmChannelWrapper::StartPolling),
          InstanceMethod("stopPolling", &ShmChannelWrapper::StopPolling),
      });

  Napi::FunctionReference* constructor = new Napi::FunctionReference();
  *constructor = Napi::Persistent(func);
  env.SetInstanceData(constructor);

  exports.Set("ShmChannel", func);
  return exports;
}

ShmChannelWrapper::ShmChannelWrapper(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<ShmChannelWrapper>(info)
{
  Napi::Env env = info.Env();

  if (info.Length() < 2) {
    Napi::TypeError::New(env, "Expected (channelId: string, options: object)")
        .ThrowAsJavaScriptException();
    return;
  }

  if (!info[0].IsString()) {
    Napi::TypeError::New(env, "channelId must be a string")
        .ThrowAsJavaScriptException();
    return;
  }

  if (!info[1].IsObject()) {
    Napi::TypeError::New(env, "options must be an object")
        .ThrowAsJavaScriptException();
    return;
  }

  std::string channel_id = info[0].As<Napi::String>().Utf8Value();
  Napi::Object options = info[1].As<Napi::Object>();

  bool is_server = false;
  bool create = false;

  if (options.Has("isServer")) {
    is_server = options.Get("isServer").ToBoolean().Value();
  }
  if (options.Has("create")) {
    create = options.Get("create").ToBoolean().Value();
  }

  channel_ = std::make_unique<ShmChannel>(channel_id, is_server, create);

  if (!channel_->is_open()) {
    Napi::Error::New(env, "Failed to open channel: " + channel_->error())
        .ThrowAsJavaScriptException();
  }
}

ShmChannelWrapper::~ShmChannelWrapper()
{
  if (polling_) {
    polling_ = false;
    if (poll_handle_) {
      uv_poll_stop(poll_handle_);
      uv_close(reinterpret_cast<uv_handle_t*>(poll_handle_),
               [](uv_handle_t* handle) {
                 delete reinterpret_cast<uv_poll_t*>(handle);
               });
      poll_handle_ = nullptr;
    }
  }
}

Napi::Value ShmChannelWrapper::IsOpen(const Napi::CallbackInfo& info)
{
  return Napi::Boolean::New(info.Env(), channel_ && channel_->is_open());
}

Napi::Value ShmChannelWrapper::GetChannelId(const Napi::CallbackInfo& info)
{
  if (!channel_) {
    return info.Env().Null();
  }
  return Napi::String::New(info.Env(), channel_->channel_id());
}

Napi::Value ShmChannelWrapper::GetError(const Napi::CallbackInfo& info)
{
  if (!channel_) {
    return Napi::String::New(info.Env(), "Channel not initialized");
  }
  return Napi::String::New(info.Env(), channel_->error());
}

Napi::Value ShmChannelWrapper::Send(const Napi::CallbackInfo& info)
{
  Napi::Env env = info.Env();

  if (!channel_ || !channel_->is_open()) {
    Napi::Error::New(env, "Channel not open").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (info.Length() < 1 || !info[0].IsTypedArray()) {
    Napi::TypeError::New(env, "Expected Uint8Array argument")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  Napi::Uint8Array data = info[0].As<Napi::Uint8Array>();
  bool success =
      channel_->send(data.Data(), static_cast<uint32_t>(data.ByteLength()));

  return Napi::Boolean::New(env, success);
}

Napi::Value ShmChannelWrapper::TryReceive(const Napi::CallbackInfo& info)
{
  Napi::Env env = info.Env();

  if (!channel_ || !channel_->is_open()) {
    return env.Null();
  }

  // Allocate a buffer for receiving (max message size)
  // In practice, we should use a pooled buffer
  static thread_local std::vector<uint8_t> recv_buffer(
      ShmChannel::MAX_MESSAGE_SIZE);

  int32_t bytes_read =
      channel_->try_receive(recv_buffer.data(), recv_buffer.size());

  if (bytes_read <= 0) {
    return env.Null(); // No data or error
  }

  // Create a new Uint8Array with the received data
  Napi::Uint8Array result = Napi::Uint8Array::New(env, bytes_read);
  memcpy(result.Data(), recv_buffer.data(), bytes_read);

  return result;
}

Napi::Value
ShmChannelWrapper::TryReceiveSSRRequest(const Napi::CallbackInfo& info)
{
  Napi::Env env = info.Env();

  if (!channel_ || !channel_->is_open()) {
    return env.Null();
  }

  // Allocate a buffer for receiving (max message size)
  static thread_local std::vector<uint8_t> recv_buffer(
      ShmChannel::MAX_MESSAGE_SIZE);

  auto read_view = channel_->get_recv_ring()->try_read_view();
  if (!read_view)
    return env.Null(); // No data

  nprpc::flat_buffer buf;
  buf.set_view_from_read(
      read_view.data, read_view.size,
      channel_->get_recv_ring(), // Pass ring buffer pointer for commit
      read_view.read_idx);

  return nprpc_node::ToJS(env, nprpc::node::flat::SSRRequest_Direct(buf, 0));
}

Napi::Value ShmChannelWrapper::SendSSRResponse(const Napi::CallbackInfo& info)
{
  Napi::Env env = info.Env();

  if (!channel_ || !channel_->is_open()) {
    Napi::Error::New(env, "Channel not open").ThrowAsJavaScriptException();
    return Napi::Boolean::New(env, false);
  }

  if (info.Length() < 1 || !info[0].IsObject()) {
    Napi::TypeError::New(env, "Expected SSRResponse object")
        .ThrowAsJavaScriptException();
    return Napi::Boolean::New(env, false);
  }

  Napi::Object respObj = info[0].As<Napi::Object>();

  constexpr size_t max_size = 10 * 1024 * 1024; // 1MB
  auto reservation =
      channel_->get_send_ring()->try_reserve_write(max_size); // 1MB
  if (!reservation) {
    std::cerr << "SharedMemoryConnection: prepare_write_buffer failed to "
                 "reserve buffer of size "
              << max_size << std::endl;
    return Napi::Boolean::New(env, false);
  }

  // Build flat buffer
  nprpc::flat_buffer fb;
  fb.set_view(reservation.data, sizeof(nprpc::node::flat::SSRResponse), reservation.max_size, nullptr,
              reservation.write_idx, true);

  auto resp = nprpc::node::flat::SSRResponse_Direct(fb, 0);
  nprpc_node::FromJS(env, respObj, resp);

  // Send
  auto data_span = fb.data();
  channel_->get_send_ring()->commit_write(reservation, data_span.size());

  return Napi::Boolean::New(env, true);
}

Napi::Value ShmChannelWrapper::HasData(const Napi::CallbackInfo& info)
{
  return Napi::Boolean::New(info.Env(), channel_ && channel_->has_data());
}

void ShmChannelWrapper::Close(const Napi::CallbackInfo& info)
{
  if (polling_) {
    polling_ = false;
    if (poll_handle_) {
      uv_poll_stop(poll_handle_);
    }
  }
  channel_.reset();
}

Napi::Value ShmChannelWrapper::StartPolling(const Napi::CallbackInfo& info)
{
  Napi::Env env = info.Env();

  if (!channel_ || !channel_->is_open()) {
    Napi::Error::New(env, "Channel not open").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (info.Length() < 1 || !info[0].IsFunction()) {
    Napi::TypeError::New(env, "Expected callback function")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  if (polling_) {
    return Napi::Boolean::New(env, true); // Already polling
  }

  // Create thread-safe function for callbacks
  tsfn_ = Napi::ThreadSafeFunction::New(env, info[0].As<Napi::Function>(),
                                        "ShmChannelCallback",
                                        0, // Unlimited queue
                                        1  // 1 thread
  );

  // Set up uv_poll for the eventfd
  poll_handle_ = new uv_poll_t;
  poll_handle_->data = this;

  uv_loop_t* loop = uv_default_loop();
  uv_poll_init(loop, poll_handle_, channel_->get_poll_fd());

  polling_ = true;

  uv_poll_start(poll_handle_, UV_READABLE,
                [](uv_poll_t* handle, int status, int events) {
                  auto* self = static_cast<ShmChannelWrapper*>(handle->data);
                  if (!self->polling_ || !self->channel_)
                    return;

                  if (status < 0) {
                    // Error
                    return;
                  }

                  if (events & UV_READABLE) {
                    // Data available - call the JS callback
                    self->tsfn_.NonBlockingCall(
                        [](Napi::Env env, Napi::Function callback) {
                          callback.Call({});
                        });
                  }
                });

  return Napi::Boolean::New(env, true);
}

Napi::Value ShmChannelWrapper::StopPolling(const Napi::CallbackInfo& info)
{
  if (polling_) {
    polling_ = false;
    if (poll_handle_) {
      uv_poll_stop(poll_handle_);
      uv_close(reinterpret_cast<uv_handle_t*>(poll_handle_),
               [](uv_handle_t* handle) {
                 delete reinterpret_cast<uv_poll_t*>(handle);
               });
      poll_handle_ = nullptr;
    }
    tsfn_.Release();
  }
  return info.Env().Undefined();
}

} // namespace nprpc_node
