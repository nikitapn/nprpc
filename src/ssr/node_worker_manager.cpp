// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#ifdef NPRPC_SSR_ENABLED

#include <nprpc/impl/node_worker_manager.hpp>
#include <nprpc/impl/nprpc_impl.hpp>

#include "../logging.hpp"
#include <nprpc_node.hpp>

#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace nprpc::impl {

namespace fs = std::filesystem;

NodeWorkerManager::NodeWorkerManager(boost::asio::io_context& ioc)
    : ioc_(ioc)
    , channel_id_(generate_channel_id())
{
}

NodeWorkerManager::~NodeWorkerManager() { stop(); }

std::string NodeWorkerManager::generate_channel_id()
{
  return "nprpc-ssr-" +
         boost::uuids::to_string(boost::uuids::random_generator()());
}

bool NodeWorkerManager::start(const std::string& handler_path,
                              const std::string& node_executable)
{
  if (running_) {
    NPRPC_LOG_ERROR("NodeWorkerManager: Already running");
    return false;
  }

  // Verify handler exists
  fs::path index_path = fs::path(handler_path) / "index.js";
  if (!fs::exists(index_path)) {
    NPRPC_LOG_ERROR("NodeWorkerManager: Handler not found: {}",
                    index_path.string());
    return false;
  }

  // Find the node executable in PATH if not an absolute path
  boost::process::filesystem::path node_path;
  if (fs::path(node_executable).is_absolute()) {
    node_path = boost::process::filesystem::path(node_executable);
  } else {
    // Search PATH for the executable
    node_path =
        boost::process::v2::environment::find_executable(node_executable);
    if (node_path.empty()) {
      NPRPC_LOG_ERROR("NodeWorkerManager: Could not find '{}' in PATH",
                      node_executable);
      return false;
    }
  }

  if (!fs::exists(node_path.string())) {
    NPRPC_LOG_ERROR("NodeWorkerManager: Node executable not found: {}",
                    node_path.string());
    return false;
  }

  try {
    // Create shared memory channel (server side - creates the rings)
    channel_ =
        std::make_unique<SharedMemoryChannel>(ioc_, channel_id_, true, true);

    if (!channel_->is_valid()) {
      NPRPC_LOG_ERROR(
          "NodeWorkerManager: Failed to create shared memory channel");
      return false;
    }

    // Set up response handler
    channel_->on_data_received_view =
        [this](const LockFreeRingBuffer::ReadView& view) {
          handle_response(view);
        };
    channel_->start_reading();

    // Determine working directory - use parent of handler_path to find
    // node_modules Structure: client/build/index.js, node_modules is at
    // client/node_modules
    fs::path working_dir = fs::path(handler_path).parent_path();

    // Build environment for Node.js process (Boost.Process v2)
    namespace bpe = boost::process::environment;
    std::unordered_map<bpe::key, bpe::value> env = {
        {"NPRPC_CHANNEL_ID", channel_id_},
    };

    // Start Node.js process
    NPRPC_LOG_INFO("NodeWorkerManager: Starting Node.js worker");
    NPRPC_LOG_INFO("  Node: {}", node_path.string());
    NPRPC_LOG_INFO("  Handler: {}", index_path.string());
    NPRPC_LOG_INFO("  Working dir: {}", working_dir.string());
    NPRPC_LOG_INFO("  Channel: {}", channel_id_);

    // Create pipes for stdout/stderr
    node_stdout_ = std::make_unique<boost::asio::readable_pipe>(ioc_);
    node_stderr_ = std::make_unique<boost::asio::readable_pipe>(ioc_);

    // Launch process with Boost.Process v2 API
    node_process_ = std::make_unique<boost::process::process>(
        ioc_, boost::process::filesystem::path(node_path),
        std::vector<std::string>{index_path.string()},
        boost::process::process_environment(env),
        boost::process::process_start_dir{working_dir.string()},
        boost::process::process_stdio{{}, *node_stdout_, *node_stderr_});

    running_ = true;

    // Start async reading from stdout/stderr
    async_read_stdout();
    async_read_stderr();

    // Give Node.js a moment to start up
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    if (!node_process_->running()) {
      NPRPC_LOG_ERROR("NodeWorkerManager: Node.js process exited prematurely");
      stop();
      return false;
    }

    NPRPC_LOG_INFO("NodeWorkerManager: Node.js worker started (PID: {})",
                   node_process_->id());

    return true;

  } catch (const std::exception& e) {
    NPRPC_LOG_ERROR("NodeWorkerManager: Failed to start: {}", e.what());
    stop();
    return false;
  }
}

void NodeWorkerManager::stop()
{
  running_ = false;

  // Terminate Node.js process
  if (node_process_ && node_process_->running()) {
    NPRPC_LOG_INFO("NodeWorkerManager: Stopping Node.js worker");

    node_process_->terminate();
    node_process_->wait();
  }
  node_process_.reset();

  // Close pipes
  if (node_stdout_ && node_stdout_->is_open()) {
    boost::system::error_code ec;
    node_stdout_->close(ec);
  }
  node_stdout_.reset();

  if (node_stderr_ && node_stderr_->is_open()) {
    boost::system::error_code ec;
    node_stderr_->close(ec);
  }
  node_stderr_.reset();

  // Clean up pending requests
  {
    std::lock_guard<std::mutex> lock(pending_mtx_);
    for (auto& [id, pending] : pending_requests_) {
      std::lock_guard<std::mutex> req_lock(pending->mtx);
      pending->completed = true;
      pending->cv.notify_all();
      if (pending->callback) {
        pending->callback(std::nullopt);
      }
    }
    pending_requests_.clear();
  }

  // Destroy channel
  channel_.reset();
}

void NodeWorkerManager::async_read_stdout()
{
  if (!running_ || !node_stdout_ || !node_stdout_->is_open())
    return;

  node_stdout_->async_read_some(
      boost::asio::buffer(stdout_buffer_),
      [this](const boost::system::error_code& ec, std::size_t bytes) {
        if (ec) {
          if (ec != boost::asio::error::operation_aborted) {
            // End of stream or error
          }
          return;
        }

        NPRPC_LOG_INFO("[Node.js] {}",
                       std::string_view(stdout_buffer_.data(), bytes));

        // Continue reading
        async_read_stdout();
      });
}

void NodeWorkerManager::async_read_stderr()
{
  if (!running_ || !node_stderr_ || !node_stderr_->is_open())
    return;

  node_stderr_->async_read_some(
      boost::asio::buffer(stderr_buffer_),
      [this](const boost::system::error_code& ec, std::size_t bytes) {
        if (ec) {
          if (ec != boost::asio::error::operation_aborted) {
            // End of stream or error
          }
          return;
        }

        NPRPC_LOG_ERROR("[Node.js] {}",
                        std::string_view(stderr_buffer_.data(), bytes));

        // Continue reading
        async_read_stderr();
      });
}

bool NodeWorkerManager::is_ready() const
{
  return running_ && node_process_ && node_process_->running() && channel_ &&
         channel_->is_valid();
}

std::optional<NodeWorkerManager::SsrResponse>
NodeWorkerManager::forward_request(const SsrRequest& request,
                                   uint32_t timeout_ms)
{
  if (!is_ready()) {
    return std::nullopt;
  }

  auto pending = std::make_shared<PendingRequest>();
  pending->deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

  uint64_t request_id = next_request_id_++;
  auto reservation = channel_->reserve_write(10 * 1024 * 1024);

  if (!reservation) {
    NPRPC_LOG_ERROR(
        "NodeWorkerManager: forward_request: Failed to reserve write buffer");
    return std::nullopt;
  }

  // Allocate the root object
  nprpc::flat_buffer fb;
  fb.set_view(reservation.data, sizeof(nprpc::node::flat::SSRRequest),
              reservation.max_size, nullptr, reservation.write_idx, true);
  auto req_builder = nprpc::node::flat::SSRRequest_Direct(fb, 0);

  // Set fields
  req_builder.id() = request_id;
  req_builder.method(request.method);
  req_builder.url(request.url);
  req_builder.clientAddress(request.client_address);

  // Headers
  req_builder.headers(static_cast<uint32_t>(request.headers.size()));
  auto headers_vec = req_builder.headers_d();
  auto headers_span = headers_vec();

  auto it = headers_span.begin();
  for (const auto& [key, value] : request.headers) {
    auto header = *it;

    header.key(key);
    header.value(value);

    ++it;
  }

  // Body
  if (!request.body.empty()) {
    req_builder.body(static_cast<uint32_t>(request.body.size()));
    auto body_vec = req_builder.body_d();
    auto body_span = body_vec();
    std::memcpy(body_span.data(), request.body.data(), request.body.size());
  } else {
    req_builder.body(0);
  }

  // Register pending request
  {
    std::lock_guard<std::mutex> lock(pending_mtx_);
    pending_requests_[request_id] = pending;
  }

  // Send request
  // The buffer data is in fb.data()

  channel_->commit_write(reservation, fb.size());

  // std::lock_guard<std::mutex> lock(pending_mtx_);
  // pending_requests_.erase(request_id);
  // return std::nullopt;

  // Wait for response - poll io_context while waiting to avoid deadlock
  // The response comes via shared memory which posts to io_context
  while (true) {
    {
      std::unique_lock<std::mutex> lock(pending->mtx);
      if (pending->completed) {
        break;
      }
      if (std::chrono::steady_clock::now() >= pending->deadline) {
        // Timeout
        std::lock_guard<std::mutex> plock(pending_mtx_);
        pending_requests_.erase(request_id);
        return std::nullopt;
      }
    }

    // Process any pending io_context work (including response callbacks)
    ioc_.poll_one();

    // Brief sleep to avoid busy spinning
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  return pending->response;
}

void NodeWorkerManager::forward_request_async(
    const SsrRequest& request,
    std::function<void(std::optional<SsrResponse>)> callback,
    uint32_t timeout_ms)
{
  if (!is_ready()) {
    if (callback)
      callback(std::nullopt);
    return;
  }

  auto pending = std::make_shared<PendingRequest>();
  pending->callback = callback;
  pending->deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

  uint64_t request_id = next_request_id_++;

  // Build request using flat buffer
  nprpc::flat_buffer fb;
  fb.prepare(4096);

  auto req_builder = nprpc::node::flat::SSRRequest_Direct(fb, 0);

  req_builder.id() = request_id;
  req_builder.method(request.method);
  req_builder.url(request.url);
  req_builder.clientAddress(request.client_address);

  // Headers
  req_builder.headers(static_cast<uint32_t>(request.headers.size()));
  auto headers_vec = req_builder.headers_d();
  auto headers_span = headers_vec();

  auto it = headers_span.begin();
  for (const auto& [key, value] : request.headers) {
    auto header = *it;

    header.key(key);
    header.value(value);

    ++it;
  }

  // Body
  if (!request.body.empty()) {
    req_builder.body(static_cast<uint32_t>(request.body.size()));
    auto body_vec = req_builder.body_d();
    auto body_span = body_vec();
    std::memcpy(body_span.data(), request.body.data(), request.body.size());
  } else {
    req_builder.body(0);
  }

  // Register pending request
  {
    std::lock_guard<std::mutex> lock(pending_mtx_);
    pending_requests_[request_id] = pending;
  }

  // Send request
  auto data_span = fb.data();
  if (!channel_->send(reinterpret_cast<const uint8_t*>(data_span.data()),
                      static_cast<uint32_t>(data_span.size()))) {
    std::lock_guard<std::mutex> lock(pending_mtx_);
    pending_requests_.erase(request_id);
    if (callback)
      callback(std::nullopt);
    return;
  }

  // Set up timeout timer
  auto timer = std::make_shared<boost::asio::steady_timer>(ioc_);
  timer->expires_at(pending->deadline);
  timer->async_wait(
      [this, request_id, pending, timer](const boost::system::error_code& ec) {
        if (ec)
          return; // Timer cancelled

        std::lock_guard<std::mutex> lock(pending_mtx_);
        auto it = pending_requests_.find(request_id);
        if (it != pending_requests_.end()) {
          pending_requests_.erase(it);
          if (pending->callback) {
            pending->callback(std::nullopt);
          }
        }
      });
}

void NodeWorkerManager::handle_response(
    const LockFreeRingBuffer::ReadView& view)
{
  try {
    nprpc::flat_buffer fb;
    fb.set_view_from_read(
        view.data, view.size,
        channel_->get_recv_ring(), // Pass ring buffer pointer for commit
        view.read_idx);

    // Read the root object
    auto resp_reader = nprpc::node::flat::SSRResponse_Direct(fb, 0);
    uint64_t request_id = resp_reader.id();
    std::shared_ptr<PendingRequest> pending;
    {
      std::lock_guard<std::mutex> lock(pending_mtx_);
      auto it = pending_requests_.find(request_id);
      if (it == pending_requests_.end()) {
        NPRPC_LOG_ERROR("NodeWorkerManager: Response for unknown request: {}",
                        request_id);
        return;
      }
      pending = it->second;
      pending_requests_.erase(it);
    }

    // Build response
    SsrResponse ssr_response;
    ssr_response.request_id = request_id;
    ssr_response.status_code = resp_reader.status();

    // Headers
    auto headers_vec = resp_reader.headers_d();
    auto headers_span = headers_vec();
    for (auto it = headers_span.begin(); it != headers_span.end(); ++it) {
      auto header = *it;
      // header.key() returns a Span<char>, which converts to string_view/string
      ssr_response.headers.emplace(std::string(header.key()),
                                   std::string(header.value()));
    }

    // Body
    auto body_vec = resp_reader.body_d();
    auto body_span = body_vec();
    if (body_span.size() > 0) {
      ssr_response.body.assign(body_span.begin(), body_span.end());
    }

    // Complete the request
    {
      std::lock_guard<std::mutex> lock(pending->mtx);
      pending->response = std::move(ssr_response);
      pending->completed = true;
    }
    pending->cv.notify_all();

    if (pending->callback) {
      pending->callback(pending->response);
    }

  } catch (const std::exception& e) {
    NPRPC_LOG_ERROR("NodeWorkerManager: Error parsing response: {}", e.what());
  }
}

// std::string NodeWorkerManager::base64_encode(const std::string& data)
// {
//   static const char* chars =
//       "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

//   std::string result;
//   result.reserve((data.size() + 2) / 3 * 4);

//   for (size_t i = 0; i < data.size(); i += 3) {
//     uint32_t n = static_cast<uint8_t>(data[i]) << 16;
//     if (i + 1 < data.size())
//       n |= static_cast<uint8_t>(data[i + 1]) << 8;
//     if (i + 2 < data.size())
//       n |= static_cast<uint8_t>(data[i + 2]);

//     result += chars[(n >> 18) & 63];
//     result += chars[(n >> 12) & 63];
//     result += (i + 1 < data.size()) ? chars[(n >> 6) & 63] : '=';
//     result += (i + 2 < data.size()) ? chars[n & 63] : '=';
//   }

//   return result;
// }

// std::string NodeWorkerManager::base64_decode(const std::string& data)
// {
//   static const int lookup[256] = {
//       -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
//       -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
//       -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63, 52, 53, 54, 55, 56, 57,
//       58, 59, 60, 61, -1, -1, -1, -1, -1, -1, -1, 0,  1,  2,  3,  4,  5,  6,
//       7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
//       25, -1, -1, -1, -1, -1, -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36,
//       37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1,
//       -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
//       -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
//       -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
//       -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
//       -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
//       -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
//       -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
//       -1, -1, -1, -1};

//   std::string result;
//   result.reserve(data.size() * 3 / 4);

//   uint32_t n = 0;
//   int bits = 0;

//   for (char c : data) {
//     if (c == '=')
//       break;
//     int v = lookup[static_cast<uint8_t>(c)];
//     if (v < 0)
//       continue;
//     n = (n << 6) | v;
//     bits += 6;
//     if (bits >= 8) {
//       bits -= 8;
//       result += static_cast<char>((n >> bits) & 0xFF);
//     }
//   }

//   return result;
// }

} // namespace nprpc::impl

#endif // NPRPC_SSR_ENABLED