#pragma once

#include "nprpc_base.hpp"

namespace nprpc {
namespace detail {
inline constexpr ObjectActivationFlags operator|(ObjectActivationFlags a, ObjectActivationFlags b) noexcept {
  return static_cast<ObjectActivationFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline constexpr ObjectActivationFlags& operator|=(ObjectActivationFlags& a, ObjectActivationFlags b) noexcept {
  return a = a | b;
}
inline constexpr ObjectActivationFlags operator^(ObjectActivationFlags a, ObjectActivationFlags b) noexcept {
  return static_cast<ObjectActivationFlags>(static_cast<uint32_t>(a) ^ static_cast<uint32_t>(b));
}
inline constexpr ObjectActivationFlags operator^=(ObjectActivationFlags& a, ObjectActivationFlags b) noexcept {
  return a = a ^ b;
}
// Returns uint32_t so `if (flags & FLAG)` works without an explicit cast
inline constexpr uint32_t operator&(ObjectActivationFlags a, ObjectActivationFlags b) noexcept {
  return static_cast<uint32_t>(a) & static_cast<uint32_t>(b);
}
} // namespace detail
using ObjectActivationFlags = detail::ObjectActivationFlags;
} // namespace nprpc