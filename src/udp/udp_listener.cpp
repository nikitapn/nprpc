// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// This file is a part of npsystem (Distributed Control System) and covered by
// LICENSING file in the topmost directory

#include <nprpc/impl/udp_listener.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include "../logging.hpp"

namespace nprpc::impl {

UdpListener::UdpListener(
  boost::asio::io_context& ioc, uint16_t port)
    : socket_(ioc,
              boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), port)),
      port_(port)
{
  // Set socket options
  socket_.set_option(boost::asio::socket_base::receive_buffer_size(65536));
  socket_.set_option(boost::asio::socket_base::reuse_address(true));

  if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
    NPRPC_LOG_INFO("[UDP Listener] Created on port {}", port);
  }
}

UdpListener::~UdpListener()
{
  stop();

  if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
    NPRPC_LOG_INFO("[UDP Listener] Destroyed");
  }
}

void UdpListener::start()
{
  if (running_) return;

  running_ = true;
  do_receive();

  if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
    NPRPC_LOG_INFO("[UDP Listener] Started on port {}", port_);
  }
}

void UdpListener::stop()
{
  if (!running_) return;

  running_ = false;

  boost::system::error_code ec;
  socket_.cancel(ec);
  socket_.close(ec);

  if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
    NPRPC_LOG_INFO("[UDP Listener] Stopped");
  }
}

void UdpListener::do_receive()
{
  if (!running_ || !socket_.is_open()) return;

  socket_.async_receive_from(
    boost::asio::buffer(recv_buffer_),
    sender_endpoint_,
    [this, self = shared_from_this()](const boost::system::error_code& ec,
                                      std::size_t bytes_received) {
      if (ec) {
        if (ec != boost::asio::error::operation_aborted) {
          if (g_cfg.debug_level >= DebugLevel::DebugLevel_Critical) {
            NPRPC_LOG_ERROR("[UDP Listener] Receive error: {}", ec.message());
          }
        }
        return;
      }

      if (bytes_received > 0) {
        handle_datagram(sender_endpoint_, bytes_received);
      }

      // Continue receiving
      do_receive();
    });
}

void UdpListener::handle_datagram(
  const endpoint_type& sender, size_t bytes_received)
{
  if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryMessageContent) {
    NPRPC_LOG_INFO("[UDP Listener] Received {} bytes from {}:{}",
                   bytes_received,
                   sender.address().to_string(),
                   sender.port());
  }

  // Validate minimum header size
  if (bytes_received < sizeof(Header)) {
    if (g_cfg.debug_level >= DebugLevel::DebugLevel_Critical) {
      NPRPC_LOG_ERROR(
        "[UDP Listener] Datagram too small: {} bytes (need at least {})",
        bytes_received,
        sizeof(Header));
    }
    return;
  }

  // Parse header
  auto* header = reinterpret_cast<const Header*>(recv_buffer_.data());

  if (header->msg_id != MessageId::FunctionCall) {
    if (g_cfg.debug_level >= DebugLevel::DebugLevel_Critical) {
      NPRPC_LOG_ERROR("[UDP Listener] Unexpected message ID: {}",
                      static_cast<int>(header->msg_id));
    }
    return;
  }

  // Validate size field matches received data
  uint32_t expected_size =
    header->size + 4;  // size field doesn't include itself
  if (expected_size != bytes_received) {
    if (g_cfg.debug_level >= DebugLevel::DebugLevel_Critical) {
      NPRPC_LOG_ERROR(
        "[UDP Listener] Size mismatch: header says {} but received {}",
        expected_size,
        bytes_received);
    }
    return;
  }

  // Parse call header to get POA and object ID
  if (bytes_received < sizeof(Header) + sizeof(flat::CallHeader)) {
    if (g_cfg.debug_level >= DebugLevel::DebugLevel_Critical) {
      NPRPC_LOG_ERROR("[UDP Listener] Datagram too small for CallHeader");
    }
    return;
  }

  auto* call_header = reinterpret_cast<const flat::CallHeader*>(
    recv_buffer_.data() + sizeof(Header));

  if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
    NPRPC_LOG_INFO(
      "[UDP Listener] Looking for object: poa={} oid={} iface={} fn={}",
      call_header->poa_idx,
      call_header->object_id,
      (int)call_header->interface_idx,
      (int)call_header->function_idx);
  }

  // Look up the object
  auto obj_guard =
    g_rpc->get_object(call_header->poa_idx, call_header->object_id);
  if (!obj_guard.has_value()) {
    NPRPC_LOG_ERROR("[UDP Listener] Object not found: poa={} oid={}",
                    call_header->poa_idx,
                    call_header->object_id);
    return;
  }

  auto* servant = obj_guard->get();
  if (!servant) {
    if (g_cfg.debug_level >= DebugLevel::DebugLevel_Critical) {
      NPRPC_LOG_ERROR("[UDP Listener] Servant is null or deleted");
    }
    return;
  }

  // Create a buffer wrapper for the received data
  // TODO: This copies the buffer, which is inefficient. Consider adding
  // a view-based Buffers variant for receive-only paths.
  flat_buffer bin(bytes_received + 128);
  flat_buffer bout(bytes_received + 128);
  auto        mb = bin.prepare(bytes_received);
  std::memcpy(mb.data(), recv_buffer_.data(), bytes_received);
  bin.commit(bytes_received);

  // Save request_id to determine if we need to send a response
  uint32_t request_id = header->request_id;

  SessionContext ctx;  // Empty context for UDP (no session)
  ctx.rx_buffer = &bin;
  ctx.tx_buffer = &bout;

  // Dispatch to servant
  try {
    servant->dispatch(ctx, false);

    // For reliable UDP calls (request_id != 0), send the response back
    if (request_id != 0) {
      auto& response_buf = bout;
      if (response_buf.size() > 0) {
        // Ensure the response has the same request_id
        auto* resp_header =
          reinterpret_cast<Header*>(response_buf.data().data());
        resp_header->request_id = request_id;

        if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
          NPRPC_LOG_INFO(
            "[UDP Listener] Sending response for request_id={} size={}",
            request_id,
            response_buf.size());
        }

        // Move response to flat_buffer for sending
        flat_buffer send_buf(response_buf.size());
        auto        src     = response_buf.cdata();
        auto        send_mb = send_buf.prepare(src.size());
        std::memcpy(send_mb.data(), src.data(), src.size());
        send_buf.commit(src.size());

        send_response(sender, std::move(send_buf));
      } else if (g_cfg.debug_level >= DebugLevel::DebugLevel_Critical) {
        NPRPC_LOG_WARN(
          "[UDP Listener] WARNING: response_buf is empty for reliable call!");
      }
    } else if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
      NPRPC_LOG_INFO("[UDP Listener] Fire-and-forget, no response sent");
    }

    if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
      NPRPC_LOG_INFO("[UDP Listener] Dispatched to {}", servant->get_class());
    }

  } catch (const std::exception& e) {
    if (g_cfg.debug_level >= DebugLevel::DebugLevel_Critical) {
      NPRPC_LOG_ERROR("[UDP Listener] Dispatch error: {}", e.what());
    }

    // For reliable calls, send error response
    if (request_id != 0) {
      flat_buffer error_buf(sizeof(Header));
      auto        error_mb   = error_buf.prepare(sizeof(Header));
      auto*       err_header = reinterpret_cast<Header*>(error_mb.data());
      err_header->size       = sizeof(Header) - 4;
      err_header->msg_id     = MessageId::Error_BadInput;
      err_header->msg_type   = MessageType::Answer;
      err_header->request_id = request_id;
      error_buf.commit(sizeof(Header));
      send_response(sender, std::move(error_buf));
    }
  }
}

void UdpListener::send_response(
  const endpoint_type& target, flat_buffer&& buffer)
{
  auto data = buffer.cdata();

  socket_.async_send_to(
    boost::asio::buffer(data.data(), data.size()),
    target,
    [this, self = shared_from_this(), buf = std::move(buffer)](
      const boost::system::error_code& ec, std::size_t bytes_sent) {
      if (ec) {
        if (g_cfg.debug_level >= DebugLevel::DebugLevel_Critical) {
          NPRPC_LOG_ERROR("[UDP Listener] Send response error: {}",
                          ec.message());
        }
      } else if (g_cfg.debug_level >=
                 DebugLevel::DebugLevel_EveryMessageContent) {
        NPRPC_LOG_INFO("[UDP Listener] Sent {} bytes response", bytes_sent);
      }
    });
}

UdpListener::endpoint_type UdpListener::local_endpoint() const
{
  boost::system::error_code ec;
  return socket_.local_endpoint(ec);
}

// Global UDP listener instance
namespace {
std::shared_ptr<UdpListener> g_udp_listener;
std::mutex                   g_udp_listener_mutex;
}  // namespace

NPRPC_API std::shared_ptr<UdpListener> start_udp_listener(
  boost::asio::io_context& ioc, uint16_t port)
{
  std::lock_guard<std::mutex> lock(g_udp_listener_mutex);

  if (!g_udp_listener || !g_udp_listener->is_running()) {
    g_udp_listener = std::make_shared<UdpListener>(ioc, port);
    g_udp_listener->start();
  }

  return g_udp_listener;
}

void init_udp_listener(
  boost::asio::io_context& ioc)
{
  if (g_cfg.listen_udp_port == 0) {
    return;  // UDP not configured
  }
  start_udp_listener(ioc, g_cfg.listen_udp_port);
}

void stop_udp_listener()
{
  std::lock_guard<std::mutex> lock(g_udp_listener_mutex);
  if (g_udp_listener) {
    g_udp_listener->stop();
    g_udp_listener.reset();
  }
}

}  // namespace nprpc::impl
