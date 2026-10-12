// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "../logging.hpp"
#include <nprpc/common.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/shared_memory_channel.hpp>
#include <nprpc/impl/shared_memory_listener.hpp>
#include <nprpc/impl/shm_fd_link.hpp>

#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/shared_memory_object.hpp>

#if defined(__linux__)
# include <cerrno>
# include <poll.h>
# include <sys/socket.h>
# include <sys/stat.h>
# include <sys/un.h>
# include <unistd.h>
#endif

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

  // Before creating anything of our own: clear out what previous servers left
  // behind when they died without unwinding.  Ours is not among it — this
  // listener's name was generated moments ago — so the sweep can run first
  // and is cheaper for doing so.
  reap_stale_shm_segments();

  // Create well-known accept ring buffer
  // Remove any existing ring from crashed server
  std::string accept_ring_name = make_shm_name(listener_name_, "accept");
  LockFreeRingBuffer::remove(accept_ring_name);

  try {
    // Small ring buffer for handshakes (10KB total - enough for ~10
    // handshakes) With variable-sized messages, this is much more efficient.
    // No explicit message limit: a handshake is a fixed struct of a few
    // hundred bytes, so what this ring can carry is limit enough.
    accept_ring_ = LockFreeRingBuffer::create(accept_ring_name,
                                              10 * 1024); // 10KB total buffer

    // Name ourselves in it.  The field is called "writer" because a channel's
    // producer is what fills it in, and on an accept ring the owner is the
    // consumer instead — but ownership is the only thing a sweep asks about,
    // and without this an accept ring belongs to nobody and outlives every
    // server that ever made one.
    {
      accept_ring_->header()->publish_writer(current_process_identity());
    }

    NPRPC_LOG_INFO("SharedMemoryListener created: {}", listener_name_);
  } catch (const std::exception& e) {
    NPRPC_LOG_ERROR("Failed to create listener ring: {}", e.what());
    throw std::runtime_error(
        std::string("SharedMemoryListener creation failed: ") + e.what());
  }

  // After the accept ring: the reaper takes a socket whose ring is gone for
  // the leftover of a dead listener.
  open_fd_socket();
}

SharedMemoryListener::~SharedMemoryListener()
{
  stop();
  close_fd_socket();

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
  if (fd_listen_socket_) {
    fd_accept_thread_ =
        std::make_unique<std::thread>([this]() { fd_accept_loop(); });
  }

  // NPRPC_LOG_INFO("SharedMemoryListener started: {}", listener_name_);
}

void SharedMemoryListener::stop()
{
  if (!running_) {
    return;
  }

  running_ = false;
  // The accept loop may be asleep with no timeout; see accept_loop for why
  // this wake cannot be missed.
  if (accept_ring_) {
    try {
      accept_ring_->wake_readers();
    } catch (...) {
    }
  }

  if (accept_thread_ && accept_thread_->joinable()) {
    accept_thread_->join();
  }

  close_fd_socket();

  NPRPC_LOG_INFO("SharedMemoryListener stopped: {}", listener_name_);
}

void SharedMemoryListener::accept_loop()
{
  char buffer[1024]; // Buffer for handshake

  while (running_) {
    try {
      // Sleep until a client knocks — no clock.  A listener is in every
      // process that serves anything, and waking it ten times a second to
      // re-check a flag was a cost every idle client paid.  The token comes
      // before the running_ check, so stop()'s wake cannot slip between the
      // check and the sleep.  Elsewhere the condvar can lose that wake, so
      // there it keeps a timeout.
      const uint32_t token = accept_ring_->wake_token();
      if (!running_)
        break;
#if defined(__linux__)
      accept_ring_->wait_for_readable(LockFreeRingBuffer::kWaitForever, token);
#else
      accept_ring_->wait_for_readable(std::chrono::milliseconds(100), token);
#endif
      size_t bytes_read = accept_ring_->try_read(buffer, sizeof(buffer));

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

    // Tell the channel whose process it is talking to, so the session can
    // reap itself if that process dies without closing the channel.
    channel->set_peer_process(handshake.client_process());

    // Its descriptor side channel, empty until the client connects to the
    // socket and names this channel — which it does only after the ready
    // flag below, so the entry is always here first.
    if (fd_listen_socket_) {
      auto link = std::make_shared<ShmFdLink>();
      channel->set_fd_link(link);
      std::lock_guard lk(fd_links_mut_);
      std::erase_if(fd_links_,
                    [](const auto& entry) { return entry.second.expired(); });
      fd_links_[channel_id] = link;
    }

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

#if defined(__linux__)

void SharedMemoryListener::open_fd_socket()
{
  const auto path = ShmFdLink::socket_path(listener_name_);
  sockaddr_un addr{};
  if (path.size() >= sizeof(addr.sun_path)) {
    NPRPC_LOG_WARN("SharedMemoryListener: socket path too long, no file "
                   "descriptor passing: {}",
                   path);
    return;
  }
  addr.sun_family = AF_UNIX;
  std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);

  UniqueFd sock(::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0));
  ::unlink(path.c_str());
  if (!sock ||
      ::bind(sock.get(), reinterpret_cast<const sockaddr*>(&addr),
             sizeof(addr)) != 0) {
    NPRPC_LOG_WARN("SharedMemoryListener: cannot create {}, no file "
                   "descriptor passing: {}",
                   path, std::strerror(errno));
    return;
  }

  // Whoever may open the accept ring may connect here, and nobody else:
  // the same mode, which the directory's setup decided.
  struct stat ring{};
  const auto ring_path = "/dev/shm" + make_shm_name(listener_name_, "accept");
  if (::stat(ring_path.c_str(), &ring) == 0)
    ::chmod(path.c_str(), ring.st_mode & 0777);

  if (::listen(sock.get(), SOMAXCONN) != 0) {
    NPRPC_LOG_WARN("SharedMemoryListener: listen on {} failed: {}", path,
                   std::strerror(errno));
    ::unlink(path.c_str());
    return;
  }

  fd_listen_socket_ = std::move(sock);
  fd_socket_path_ = path;
}

void SharedMemoryListener::close_fd_socket()
{
  if (!fd_listen_socket_)
    return;
  // Wakes the accept() below with an error; running_ is already false.
  ::shutdown(fd_listen_socket_.get(), SHUT_RDWR);
  if (fd_accept_thread_ && fd_accept_thread_->joinable())
    fd_accept_thread_->join();
  fd_accept_thread_.reset();
  ::unlink(fd_socket_path_.c_str());
  fd_listen_socket_.reset();
}

void SharedMemoryListener::fd_accept_loop()
{
  while (running_) {
    UniqueFd connection(
        ::accept4(fd_listen_socket_.get(), nullptr, nullptr, SOCK_CLOEXEC));
    if (connection) {
      handle_fd_connection(std::move(connection));
      continue;
    }
    if (!running_)
      break;
    if (errno == EINTR || errno == ECONNABORTED)
      continue;
    if (errno == EMFILE || errno == ENFILE || errno == ENOBUFS ||
        errno == ENOMEM) {
      // Out of descriptors for now: the connection waits in the backlog.
      NPRPC_LOG_WARN("SharedMemoryListener: accept on {}: {}",
                     fd_socket_path_, std::strerror(errno));
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }
    NPRPC_LOG_ERROR("SharedMemoryListener: accept on {} failed: {}",
                    fd_socket_path_, std::strerror(errno));
    break;
  }
}

void SharedMemoryListener::handle_fd_connection(UniqueFd connection)
{
  // The client sends its hello right after connecting; one that does not
  // is not worth holding up everybody else for.
  pollfd pfd{connection.get(), POLLIN, 0};
  if (::poll(&pfd, 1, 1000) != 1)
    return;

  ShmFdHello hello;
  if (::recv(connection.get(), &hello, sizeof(hello), 0) !=
          static_cast<ssize_t>(sizeof(hello)) ||
      hello.magic != ShmFdHello::MAGIC ||
      hello.version != ShmFdHello::VERSION)
    return;
  hello.channel_id[sizeof(hello.channel_id) - 1] = '\0';

  // Each channel takes one connection: the first to name it.  Its id is in
  // the ring names, so it is no secret from anyone who can list the
  // directory — but they could open the rings themselves too.
  std::shared_ptr<ShmFdLink> link;
  {
    std::lock_guard lk(fd_links_mut_);
    if (auto it = fd_links_.find(hello.channel_id); it != fd_links_.end()) {
      link = it->second.lock();
      fd_links_.erase(it);
    }
  }

  const int raw = connection.get();
  // Attached before the answer goes out: from the moment the client has it
  // a message with descriptors may arrive, and the reader must find them.
  if (link && link->attach(std::move(connection))) {
    const uint32_t accepted = ShmFdHello::kAccepted;
    ::send(raw, &accepted, sizeof(accepted), MSG_NOSIGNAL);
  } else if (connection) {
    const uint32_t refused = 0;
    ::send(connection.get(), &refused, sizeof(refused), MSG_NOSIGNAL);
  }
}

#else // !__linux__

void SharedMemoryListener::open_fd_socket() {}
void SharedMemoryListener::close_fd_socket() {}
void SharedMemoryListener::fd_accept_loop() {}
void SharedMemoryListener::handle_fd_connection(UniqueFd) {}

#endif

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
  // Name ourselves so the server can detect it if this process is killed
  // while the channel is open.
  const auto self_id = current_process_identity();
  handshake.client_pid = self_id.pid;
  handshake.client_start_token = self_id.start_token;
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
    auto last_log = start;
    while (ready_flag->load(std::memory_order_acquire) == 0u) {
#if defined(__x86_64__) || defined(__i386__)
      __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
      asm volatile("yield" ::: "memory");
#endif
      auto now = std::chrono::steady_clock::now();
      if (now - last_log >= std::chrono::milliseconds(500)) {
        NPRPC_LOG_WARN("SHM connect: still waiting for ready flag '{}' ({} ms)",
                       ready_flag_name,
                       std::chrono::duration_cast<std::chrono::milliseconds>(
                           now - start).count());
        last_log = now;
      }
      if (now - start > std::chrono::seconds(5)) {
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
    // Descriptor side channel, if the server offers one.
    channel->set_fd_link(ShmFdLink::connect(listener_name, channel_id));
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
