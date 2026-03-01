// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

#include <nprpc/common.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/stream_manager.hpp>

#include "helper.hpp"

namespace nprpc::impl {

void SocketConnection::send_receive(flat_buffer& buffer, uint32_t timeout_ms)
{
  assert(*(uint32_t*)buffer.data().data() == buffer.size() - 4);

  // dump_message(buffer, false);

  struct WorkImpl : IOWork {
    flat_buffer& buf;
    SocketConnection& this_;
    uint32_t timeout_ms;

    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    boost::system::error_code result;

    void operator()() noexcept override
    {
      this_.set_timeout(timeout_ms);
      this_.write_async(buf, [&](const boost::system::error_code& ec,
                                 size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);
        if (ec) {
          on_failed(ec);
          this_.pop_and_execute_next_task();
          return;
        }
        this_.do_read_size();
      });
    }

    void on_failed(const boost::system::error_code& ec) noexcept override
    {
      {
        std::lock_guard<std::mutex> lock(mtx);
        result = ec;
        done = true;
      }
      cv.notify_one();
    }

    void on_executed() noexcept override
    {
      {
        std::lock_guard<std::mutex> lock(mtx);
        result = boost::system::error_code{};
        done = true;
      }
      cv.notify_one();
    }

    flat_buffer& buffer() noexcept override { return buf; };

    boost::system::error_code wait()
    {
      std::unique_lock<std::mutex> lock(mtx);
      cv.wait(lock, [this] { return done; });
      return result;
    }

    WorkImpl(flat_buffer& _buf, SocketConnection& _this_, uint32_t _timeout_ms)
        : buf(_buf)
        , this_(_this_)
        , timeout_ms(_timeout_ms)
    {
    }
  };

  // Post work and wait for completion
  auto w = std::make_shared<WorkImpl>(buffer, *this, timeout_ms);
  add_work(w);
  auto ec = w->wait();

  if (!ec) {
    // dump_message(buffer, true);
    return;
  }

  if (ec == boost::asio::error::connection_reset ||
      ec == boost::asio::error::broken_pipe) {
    reconnect();
    auto w = std::make_shared<WorkImpl>(buffer, *this, timeout_ms);
    add_work(w);
    auto ec = w->wait();
    if (ec)
      close();
  } else {
    fail(ec, "send_receive");
    close();
    throw nprpc::ExceptionCommFailure();
  }
}

void SocketConnection::send_receive_async(
    flat_buffer&& buffer,
    std::optional<std::function<void(const boost::system::error_code&,
                                     flat_buffer&)>>&& completion_handler,
    uint32_t timeout_ms)
{
  assert(*(uint32_t*)buffer.data().data() == buffer.size() - 4);

  struct WorkImpl : IOWork {
    flat_buffer buf;
    std::shared_ptr<SocketConnection> this_;
    uint32_t timeout_ms;
    std::optional<
        std::function<void(const boost::system::error_code&, flat_buffer&)>>
        handler;

    void operator()() noexcept override
    {
      this_->set_timeout(timeout_ms);
      this_->write_async(buf, [&](const boost::system::error_code& ec,
                                  size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);
        if (ec) {
          on_failed(ec);
          this_->pop_and_execute_next_task();
          return;
        }
        if (!handler) {
          // Fire-and-forget: pop immediately and start streaming listener mode
          this_->pop_and_execute_next_task();
          // Continue reading for streaming messages
          this_->do_read_size();
        } else {
          // Normal request-response: wait for reply
          this_->do_read_size();
        }
      });
    }

    void on_failed(const boost::system::error_code& ec) noexcept override
    {
      if (handler)
        handler.value()(ec, buf);
    }

    void on_executed() noexcept override
    {
      if (handler)
        handler.value()(boost::system::error_code{}, buf);
    }

    flat_buffer& buffer() noexcept override { return buf; };

    WorkImpl(flat_buffer&& _buf,
             std::shared_ptr<SocketConnection> _this_,
             std::optional<std::function<void(const boost::system::error_code&,
                                              flat_buffer&)>>&& _handler,
             uint32_t _timeout_ms)
        : buf(std::move(_buf))
        , this_(_this_)
        , timeout_ms(_timeout_ms)
        , handler(std::move(_handler))
    {
    }
  };

  add_work(std::make_shared<WorkImpl>(std::move(buffer), shared_from_this(),
                                      std::move(completion_handler),
                                      timeout_ms));
}

void SocketConnection::reconnect()
{
  socket_ = std::move(net::ip::tcp::socket(socket_.get_executor()));

  boost::system::error_code ec;
  socket_.connect(endpoint_, ec);

  if (ec) {
    close();
    throw nprpc::ExceptionCommFailure();
  }
}

void SocketConnection::do_read_size()
{
  auto& buf = current_rx_buffer();
  buf.consume(buf.size());

  timeout_timer_.expires_after(timeout_);
  socket_.async_read_some(buf.prepare(1024),
                           std::bind(&SocketConnection::on_read_size,
                                     shared_from_this(), std::placeholders::_1,
                                     std::placeholders::_2));
}

void SocketConnection::do_read_body()
{
  // Set timeout once for the entire body read — NOT per-chunk.
  // async_read reads exactly rx_size_ bytes in a single composed operation,
  // avoiding ~2*N timer cancel/reschedule + io_context dispatches (where N is
  // the number of OS TCP segments for the message body, ~80 for 10MB default).
  timeout_timer_.expires_after(timeout_);
  boost::asio::async_read(socket_,
                          current_rx_buffer().prepare(rx_size_),
                          std::bind(&SocketConnection::on_read_body,
                                    shared_from_this(), std::placeholders::_1,
                                    std::placeholders::_2));
}

void SocketConnection::on_read_size(const boost::system::error_code& ec,
                                    size_t len)
{
  timeout_timer_.expires_after(std::chrono::system_clock::duration::max());

  if (ec) {
    fail(ec, "client_socket_session: on_read_size");
    if (!wq_.empty()) {
      (*wq_.front()).on_failed(ec);
      pop_and_execute_next_task();
    }
    return;
  }

 if (len < 16) {
    fail(boost::asio::error::invalid_argument, "read size header");
    return;
  }

  auto& buf = current_rx_buffer();
  auto body_len = *(uint32_t*)buf.data().data();

  if (body_len > max_message_size) {
    fail(boost::asio::error::no_buffer_space, "rx_size_ > max_message_size");
    if (!wq_.empty()) {
      (*wq_.front()).on_failed(ec);
      pop_and_execute_next_task();
    }
    return;
  }

  if (body_len == len - 4) {
    // No more data to read, process immediately
    buf.commit(len);
    rx_size_ = 0;
    on_read_body(boost::system::error_code(), 0);
  } else {
    rx_size_ = body_len;
    buf.commit(4);
    on_read_body(boost::system::error_code(), len - 4);
  }
}

void SocketConnection::on_read_body(const boost::system::error_code& ec,
                                    size_t len)
{
  timeout_timer_.expires_after(std::chrono::system_clock::duration::max());

  if (ec) {
    fail(ec, "client_socket_session: on_read_body");
    if (!wq_.empty()) {
      (*wq_.front()).on_failed(ec);
      pop_and_execute_next_task();
    }
    return;
  }

  auto& buf = current_rx_buffer();

  buf.commit(len);
  rx_size_ -= static_cast<uint32_t>(len);

  if (rx_size_ != 0) {
    do_read_body();
  } else {
    // Check if this is a streaming message that should be routed to stream_manager
    auto* header = reinterpret_cast<const flat::Header*>(buf.data().data());
    bool is_stream_msg = (header->msg_id == MessageId::StreamDataChunk ||
                          header->msg_id == MessageId::StreamCompletion ||
                          header->msg_id == MessageId::StreamError);
    
    if (is_stream_msg && ctx_.stream_manager) {
      // Route to stream_manager instead of treating as RPC response
      if (header->msg_id == MessageId::StreamDataChunk) {
        flat_buffer chunk_buf;
        chunk_buf.prepare(buf.size());
        chunk_buf.commit(buf.size());
        std::memcpy(chunk_buf.data().data(), buf.data().data(), buf.size());
        ctx_.stream_manager->on_chunk_received(std::move(chunk_buf));
      } else if (header->msg_id == MessageId::StreamCompletion) {
        flat::StreamComplete_Direct msg(buf, sizeof(flat::Header));
        ctx_.stream_manager->on_stream_complete(msg.stream_id());
      } else if (header->msg_id == MessageId::StreamError) {
        flat::StreamError_Direct msg(buf, sizeof(flat::Header));
        flat_buffer error_buf;  // Empty for now
        ctx_.stream_manager->on_stream_error(msg.stream_id(), msg.error_code(), std::move(error_buf));
      }
      // Continue reading for more streaming messages
      do_read_size();
    } else if (!wq_.empty()) {
      // Regular RPC response
      (*wq_.front()).on_executed();
      pop_and_execute_next_task();
    } else {
      // Unexpected message while in streaming mode - just continue reading
      do_read_size();
    }
  }
}

SocketConnection::SocketConnection(const EndPoint& endpoint,
                                   boost::asio::ip::tcp::socket&& socket)
    : Session(socket.get_executor())
    , socket_{std::move(socket)}
{
  ctx_.remote_endpoint = endpoint;
  timeout_timer_.expires_after(std::chrono::system_clock::duration::max());
  endpoint_ = sync_socket_connect(endpoint, socket_);

  // Large socket buffers: avoids ~80 TCP-window-fill cycles for 10MB messages.
  // gRPC uses 4MB by default; without this, auto-tuning takes several RTTs.
  constexpr int kLargeBuf = 4 * 1024 * 1024;
  boost::system::error_code ec;
  socket_.set_option(boost::asio::socket_base::receive_buffer_size(kLargeBuf), ec);
  socket_.set_option(boost::asio::socket_base::send_buffer_size(kLargeBuf), ec);

  start_timeout_timer();
}

} // namespace nprpc::impl