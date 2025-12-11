#pragma once
#include <napi.h>
#include <nprpc_node.hpp>

namespace nprpc_node {

inline Napi::Value ToJS(Napi::Env env, nprpc::node::flat::SSRRequest_Direct req)
{
  auto obj = Napi::Object::New(env);
  obj.Set("type", Napi::String::New(env, "request"));
  obj.Set("id", Napi::Number::New(env, req.id()));
  obj.Set("method", Napi::String::New(env, std::string(req.method())));
  obj.Set("url", Napi::String::New(env, std::string(req.url())));
  // Copy headers
  auto headers = Napi::Object::New(env);
  auto headers_span = req.headers();
  for (auto it = headers_span.begin(); it != headers_span.end(); ++it) {
    auto h = *it;
    headers.Set(std::string(h.key()),
                Napi::String::New(env, std::string(h.value())));
  }
  obj.Set("headers", headers);
  // Copy body
  auto body_span = req.body();
  if (body_span.size() > 0) {
    auto body = Napi::Uint8Array::New(env, body_span.size());
    std::memcpy(body.Data(), body_span.data(), body_span.size());
    obj.Set("body", body);
  } else {
    obj.Set("body", env.Null());
  }
  if (req.clientAddress().size() > 0) {
    obj.Set("clientAddress",
            Napi::String::New(env, std::string(req.clientAddress())));
  } else {
    obj.Set("clientAddress", env.Null());
  }
  return obj;
}

inline void FromJS(Napi::Env env,
                   const Napi::Value val,
                   nprpc::node::flat::SSRResponse_Direct& resp)
{
  auto obj = val.As<Napi::Object>();
  resp.id() = obj.Get("id").As<Napi::Number>().Uint32Value();
  resp.status() = obj.Get("status").As<Napi::Number>().Int32Value();
  // Copy headers
  auto headers = obj.Get("headers").As<Napi::Object>();
  auto headers_prop_names = headers.GetPropertyNames();
  const uint32_t len = headers_prop_names.Length();
  resp.headers(len);
  auto headers_span = resp.headers();
  auto it = headers_span.begin();
  for (uint32_t i = 0; i < len; ++i) {
    auto h = *it;
    auto key = headers_prop_names.Get(i).As<Napi::String>().Utf8Value();
    auto value = headers.Get(key).As<Napi::String>().Utf8Value();
    h.key(key);
    h.value(value);
    ++it;
  }
  // Copy body
  auto bodyValue = obj.Get("body");
  if (bodyValue.IsNull() || bodyValue.IsUndefined()) {
    resp.body(0);
  } else if (bodyValue.IsBuffer()) {
    auto body = bodyValue.As<Napi::Buffer<uint8_t>>();
    resp.body(body.ByteLength());
    auto body_span = resp.body();
    std::memcpy(body_span.data(), body.Data(), body.ByteLength());
  } else if (bodyValue.IsTypedArray()) {
    auto body = bodyValue.As<Napi::Uint8Array>();
    resp.body(body.ByteLength());
    auto body_span = resp.body();
    std::memcpy(body_span.data(), body.Data(), body.ByteLength());
  } else if (bodyValue.IsString()) {
    auto bodyStr = bodyValue.As<Napi::String>().Utf8Value();
    resp.body(static_cast<uint32_t>(bodyStr.size()));
    auto body_span = resp.body();
    std::memcpy(body_span.data(), bodyStr.data(), bodyStr.size());
  } else {
    throw Napi::TypeError::New(env, "body must be null, Buffer, Uint8Array or string. Got type: " +
                                     std::to_string(bodyValue.Type()));
  }
}

} // namespace nprpc_node
