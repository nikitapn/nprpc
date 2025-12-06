// Stub for nprpc globals needed by lock_free_ring_buffer.cpp
// This allows the Node.js addon to link without the full nprpc library

#include <nprpc/impl/nprpc_impl.hpp>

namespace nprpc::impl {
// Default configuration with debug disabled for the Node.js addon
// We only need to satisfy the g_cfg reference used in lock_free_ring_buffer.cpp
Config g_cfg;
}
