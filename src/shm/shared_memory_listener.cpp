// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "../logging.hpp"
#include <nprpc/common.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/shared_memory_channel.hpp>
#include <nprpc/impl/shared_memory_listener.hpp>

#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/shared_memory_object.hpp>

namespace nprpc::impl {

SharedMemoryListener::SharedMemoryListener(boost::asio::io_context& ioc,
                                           const std::string& listener_name,
                                           AcceptHandler accept_handler)
    : listener_name_(listener_name)
    , ioc_(ioc)
    , accept_handler_(std::move(accept_handler))
{
  if (listener_name_.empty()) {
    throw std::invalid_argument("Listener name cannot be empty");
  }

  if (!accept_handler_) {
    throw std::invalid_argument("Accept handler cannot be null");
  }

  // Create well-known accept ring buffer
  // Remove any existing ring from crashed server
  std::string accept_ring_name = make_shm_name(listener_name_, "accept");
  LockFreeRingBuffer::remove(accept_ring_name);

  try {
    // Small ring buffer for handshakes (10KB total - enough for ~10
    // handshakes) With variable-sized messages, this is much more efficient
    accept_ring_ = LockFreeRingBuffer::create(accept_ring_name,
                                              10 * 1024); // 10KB total buffer

    NPRPC_LOG_INFO("SharedMemoryListener created: {}", listener_name_);
  } catch (const std::exception& e) {
    NPRPC_LOG_ERROR("Failed to create listener ring: {}", e.what());
    throw std::runtime_error(
        std::string("SharedMemoryListener creation failed: ") + e.what());
  }
}

SharedMemoryListener::~SharedMemoryListener()
{
  stop();

  // Clean up accept ring
  accept_ring_.reset();

  try {
    std::string accept_ring_name = make_shm_name(listener_name_, "accept");

    // NPRPC_LOG_INFO("SharedMemoryListener cleaned up: {}", listener_name_);
  } catch (const std::exception& e) {
    // Ignore cleanup errors
  }
}

void SharedMemoryListener::start()
{
  if (running_) {
    return;
  }

  running_ = true;
  accept_thread_ = std::make_unique<std::thread>([this]() { accept_loop(); });

  // NPRPC_LOG_INFO("SharedMemoryListener started: {}", listener_name_);
}

void SharedMemoryListener::stop()
{
  if (!running_) {
    return;
  }

  running_ = false;

  if (accept_thread_ && accept_thread_->joinable()) {
    accept_thread_->join();
  }

  NPRPC_LOG_INFO("SharedMemoryListener stopped: {}", listener_name_);
}

void SharedMemoryListener::accept_loop()
{
  char buffer[1024]; // Buffer for handshake

  while (running_) {
    try {
      // Wait for connection request with timeout
      size_t bytes_read = accept_ring_->read_with_timeout(
          buffer, sizeof(buffer), std::chrono::milliseconds(100));

      if (bytes_read > 0) {
        // Validate handshake size
        if (bytes_read != sizeof(SharedMemoryHandshake)) {
          NPRPC_LOG_ERROR("SharedMemoryListener: Invalid handshake size: {}",
                          bytes_read);
          continue;
        }

        // Parse handshake
        SharedMemoryHandshake handshake;
        std::memcpy(&handshake, buffer, sizeof(SharedMemoryHandshake));

        if (!handshake.is_valid()) {
          NPRPC_LOG_ERROR("SharedMemoryListener: Invalid handshake "
                          "magic/version");
          continue;
        }

        // Handle the connection request
        handle_connection_request(handshake);
      }
    } catch (const std::exception& e) {
      if (running_) {
        NPRPC_LOG_ERROR("SharedMemoryListener accept error: {}", e.what());
      }
      break;
    }
  }

  NPRPC_LOG_INFO("SharedMemoryListener accept loop exiting");
}

void SharedMemoryListener::handle_connection_request(
    const SharedMemoryHandshake& handshake)
{
  std::string channel_id(handshake.channel_id);
  std::string ready_flag_shm(handshake.ready_flag_shm);

  NPRPC_LOG_INFO("SharedMemoryListener: Accepting connection on channel: {}",
                 channel_id);

  try {
    // Create dedicated channel for this client (server creates the rings)
    auto channel = std::make_unique<SharedMemoryChannel>(ioc_, channel_id,
                                                         /*is_server=*/true,
                                                         /*create_rings=*/true);

    NPRPC_LOG_INFO("SharedMemoryListener: Channel created successfully: {}",
                   channel_id);

    // Wire up on_data_received BEFORE signaling the client.
    // The channel's read_thread starts inside the ctor, so once the client
    // is unblocked it can immediately send data — the handler must already
    // be set at that point.
    if (accept_handler_) {
      accept_handler_(std::move(channel));
    }

    // Signal the client that the rings exist AND the server-side handler is
    // fully installed. The release-store is the synchronization barrier:
    // everything above is sequenced-before this store, and the client's
    // acquire-load ensures it sees all of it.
    try {
      namespace bip = boost::interprocess;
      bip::shared_memory_object ready_shm(
          bip::open_only, ready_flag_shm.c_str(), bip::read_write);
      bip::mapped_region ready_region(ready_shm, bip::read_write);
      auto* flag = static_cast<std::atomic<uint32_t>*>(ready_region.get_address());
      flag->store(1u, std::memory_order_release);
    } catch (const std::exception& e) {
      NPRPC_LOG_ERROR("SharedMemoryListener: Failed to signal ready flag: {}",
                      e.what());
    }

  } catch (const std::exception& e) {
    NPRPC_LOG_ERROR("SharedMemoryListener: Failed to create channel: {}",
                    e.what());
  }
}

// Client-side connection establishment
std::unique_ptr<SharedMemoryChannel>
connect_to_shared_memory_listener(boost::asio::io_context& ioc,
                                  const std::string& listener_name)
{
  if (listener_name.empty()) {
    throw std::invalid_argument("Listener name cannot be empty");
  }

  // Generate unique channel ID for this connection
  std::string channel_id = SharedMemoryChannel::generate_channel_id();

  NPRPC_LOG_INFO("Connecting to listener: {} with channel: {}", listener_name,
                 channel_id);

  // Create a one-page shm segment holding a single atomic<uint32_t>.
  // The server will store 1 into it (release) once the ring buffers exist,
  // so we can spin on it (acquire) instead of polling with sleeps.
  namespace bip = boost::interprocess;
  std::string ready_flag_name = "/nprpc_ready_" + channel_id;

  bip::shared_memory_object::remove(ready_flag_name.c_str()); // clean up any stale
  bip::shared_memory_object ready_shm(
      bip::create_only, ready_flag_name.c_str(), bip::read_write);
  ready_shm.truncate(sizeof(std::atomic<uint32_t>));
  bip::mapped_region ready_region(ready_shm, bip::read_write);
  auto* ready_flag = new (ready_region.get_address()) std::atomic<uint32_t>(0);

  // RAII cleanup of the ready-flag shm on scope exit
  auto cleanup_ready = [&]() noexcept {
    try { bip::shared_memory_object::remove(ready_flag_name.c_str()); }
    catch (...) {}
  };

  // Prepare handshake
  SharedMemoryHandshake handshake;
  std::strncpy(handshake.channel_id, channel_id.c_str(),
               sizeof(handshake.channel_id) - 1);
  handshake.channel_id[sizeof(handshake.channel_id) - 1] = '\0';
  std::strncpy(handshake.ready_flag_shm, ready_flag_name.c_str(),
               sizeof(handshake.ready_flag_shm) - 1);
  handshake.ready_flag_shm[sizeof(handshake.ready_flag_shm) - 1] = '\0';

  try {
    // Open the listener's accept ring and send the connection request
    std::string accept_ring_name = make_shm_name(listener_name, "accept");
    auto accept_ring = LockFreeRingBuffer::open(accept_ring_name);

    if (!accept_ring->try_write(&handshake, sizeof(handshake))) {
      cleanup_ready();
      throw std::runtime_error("Failed to send connection request to "
                               "listener (ring buffer full)");
    }

    NPRPC_LOG_INFO("Sent connection request, waiting for server ready signal...");

    // Spin-wait for the server's release-store on the ready flag.
    auto start = std::chrono::steady_clock::now();
    while (ready_flag->load(std::memory_order_acquire) == 0u) {
#if defined(__x86_64__) || defined(__i386__)
      __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
      asm volatile("yield" ::: "memory");
#endif
      auto elapsed = std::chrono::steady_clock::now() - start;
      if (elapsed > std::chrono::seconds(5)) {
        cleanup_ready();
        throw std::runtime_error(
            "Timeout waiting for server to create ring buffers");
      }
    }
    // At this point the server has completed all mmap/ring-buffer setup
    // (sequenced-before the release store), so we can safely open them.
    auto channel = std::make_unique<SharedMemoryChannel>(ioc, channel_id,
                                                         /*is_server=*/false,
                                                         /*create_rings=*/false);
    // Caller sets on_data_received[_view] and then calls start_reading().
    // We don't start it here because the callback isn't wired yet.
    cleanup_ready();

    NPRPC_LOG_INFO("Connected to listener with dedicated channel: {}",
                   channel_id);

    return channel;

  } catch (const std::exception& e) {
    cleanup_ready();
    NPRPC_LOG_ERROR("Failed to connect to listener: {}", e.what());
    throw std::runtime_error(std::string("Connection failed: ") + e.what());
  }
}

} // namespace nprpc::impl
