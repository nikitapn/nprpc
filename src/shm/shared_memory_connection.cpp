// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <iostream>

#include <nprpc/common.hpp>
#include <nprpc/impl/shared_memory_connection.hpp>
#include <nprpc/impl/shared_memory_listener.hpp>

namespace nprpc::impl {

void SharedMemoryConnection::timeout_action()
{
  // For shared memory, timeout means the other side is unresponsive
  // std::cerr << "SharedMemoryConnection timeout" << std::endl;
  // close();
}

void SharedMemoryConnection::add_work(std::shared_ptr<IOWork> w)
{
  boost::asio::post(ioc_, [w{std::move(w)}, this]() mutable {
    std::lock_guard lock(mutex_);
    wq_.push_back(std::move(w));
    if (wq_.size() == 1)
      (*wq_.front())();
  });
}

void SharedMemoryConnection::send_receive(flat_buffer& buffer,
                                          uint32_t timeout_ms)
{
  assert(*(uint32_t*)buffer.data().data() == buffer.size() - 4);

  // dump_message(buffer, false);

  struct work_impl : IOWork {
    flat_buffer& buf;
    SharedMemoryConnection& this_;
    uint32_t timeout_ms;
    LockFreeRingBuffer::WriteReservation reservation; // For zero-copy write

    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    boost::system::error_code result;

    void operator()() noexcept override
    {
      this_.set_timeout(timeout_ms);

      // Validate message size (security check)
      if (buf.size() > max_message_size) {
        std::cerr << "SharedMemoryConnection: Message too large: " << buf.size()
                  << std::endl;
        on_failed(boost::system::error_code(boost::asio::error::message_size,
                                            boost::system::system_category()));
        this_.pop_and_execute_next_task();
        return;
      }

      // Check if this is a zero-copy write (buffer in view mode with
      // active reservation)
      if (buf.is_view_mode() && reservation.valid) {
        // Zero-copy path: data is already in ring buffer, just commit
        this_.channel_->commit_write(reservation, buf.size());
      } else {
        // Normal path: copy data to ring buffer
        if (!this_.channel_->send(buf.data().data(), buf.size())) {
          on_failed(
              boost::system::error_code(boost::asio::error::no_buffer_space,
                                        boost::system::system_category()));
          this_.pop_and_execute_next_task();
          return;
        }
      }
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
      // std::cout << "SharedMemoryConnection: send_receive completed
      // successfully" << std::endl;
      {
        std::lock_guard<std::mutex> lock(mtx);
        result = boost::system::error_code{};
        done = true;
      }
      // std::cout << "Notifying waiting thread..." << std::endl;
      cv.notify_one();
    }

    boost::system::error_code wait()
    {
      std::unique_lock<std::mutex> lock(mtx);
      cv.wait(lock, [this] { return done; });
      return result;
    }

    flat_buffer& buffer() noexcept override { return buf; }

    work_impl(flat_buffer& _buf,
              SharedMemoryConnection& _this_,
              uint32_t _timeout_ms,
              LockFreeRingBuffer::WriteReservation _reservation)
        : buf(_buf)
        , this_(_this_)
        , timeout_ms(_timeout_ms)
        , reservation(_reservation)
    {
    }
  };

  // Transfer reservation from connection to work item
  auto reservation = std::exchange(active_reservation_, {});
  // Post work and wait for completion
  // Work remains owned by the connection until completion
  auto w = std::make_shared<work_impl>(buffer, *this, timeout_ms, reservation);
  add_work(w);
  auto ec = w->wait();

  // std::cout << "SharedMemoryConnection: send_receive finished with ec=" <<
  // ec.message() << std::endl;

  if (!ec) {
    // dump_message(buffer, true);
    return;
  }

  fail(ec, "send_receive");
  close();
  throw nprpc::ExceptionCommFailure();
}

void SharedMemoryConnection::send_receive_async(
    flat_buffer&& buffer,
    std::optional<std::function<void(const boost::system::error_code&,
                                     flat_buffer&)>>&& completion_handler,
    uint32_t timeout_ms)
{
  assert(*(uint32_t*)buffer.data().data() == buffer.size() - 4);

  pending_requests_++;

  struct work_impl : IOWork {
    flat_buffer buf;
    SharedMemoryConnection& this_;
    uint32_t timeout_ms;
    std::optional<
        std::function<void(const boost::system::error_code&, flat_buffer&)>>
        handler;

    void operator()() noexcept override
    {
      this_.set_timeout(timeout_ms);

      // Validate message size (security check)
      if (buf.size() > max_message_size) {
        std::cerr << "SharedMemoryConnection: Message too large: " << buf.size()
                  << std::endl;
        on_failed(boost::system::error_code(boost::asio::error::message_size,
                                            boost::system::system_category()));
        this_.pop_and_execute_next_task();
        return;
      }

      if (!this_.channel_->send(buf.data().data(), buf.size())) {
        on_failed(boost::system::error_code(boost::asio::error::no_buffer_space,
                                            boost::system::system_category()));
        this_.pop_and_execute_next_task();
        return;
      }
    }

    void on_failed(const boost::system::error_code& ec) noexcept override
    {
      {
        std::lock_guard lock(this_.mutex_);
        this_.pending_requests_--;
      }
      if (handler)
        handler.value()(ec, buf);
    }

    void on_executed() noexcept override
    {
      {
        std::lock_guard lock(this_.mutex_);
        this_.pending_requests_--;
      }
      if (handler)
        handler.value()(boost::system::error_code{}, buf);
    }

    flat_buffer& buffer() noexcept override { return buf; }

    work_impl(flat_buffer&& _buf,
              SharedMemoryConnection& _this_,
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

  add_work(std::make_shared<work_impl>(
      std::move(buffer), *this, std::move(completion_handler), timeout_ms));
}

SharedMemoryConnection::SharedMemoryConnection(const EndPoint& endpoint,
                                               boost::asio::io_context& ioc)
    : Session(ioc.get_executor())
    , ioc_(ioc)
{
  ctx_.remote_endpoint = endpoint;
  timeout_timer_.expires_at(boost::posix_time::pos_infin);

  // Parse endpoint to extract listener name
  // Expected format: mem://listener_name (no port needed for shared memory)
  std::string listener_name(endpoint.memory_channel_id());
  if (listener_name.empty()) {
    throw nprpc::Exception(
        "Invalid shared memory endpoint: missing listener name");
  }

  // Connect to the listener, which will establish a dedicated channel
  try {
    channel_ = connect_to_shared_memory_listener(ioc_, listener_name);
  } catch (const std::exception& e) {
    std::string error_msg = "Could not connect to shared memory listener: ";
    error_msg += e.what();
    throw nprpc::Exception(error_msg.c_str());
  }

  // Use zero-copy reads by default
  channel_->on_data_received_view =
      [this](const LockFreeRingBuffer::ReadView& read_view) {
        std::lock_guard lock(mutex_);
        if (wq_.empty()) {
          // std::cerr << "SharedMemoryConnection: Received unsolicited
          // response"
          // << std::endl;
          return;
        }

        // Validate header size (security check)
        if (read_view.size < sizeof(impl::flat::Header)) {
          // std::cerr << "SharedMemoryConnection: Message too small: " <<
          // read_view.size << std::endl;
          return;
        }

        // Zero-copy: create a view directly into the ring buffer
        // The flat_buffer will track the ReadView and commit_read when done
        auto& current_buffer = current_rx_buffer();
        // std::cout << "SharedMemoryConnection: Received zero-copy message of
        // size "
        //           << read_view.size << std::endl;
        current_buffer.consume(current_buffer.size());
        current_buffer.set_view_from_read(
            read_view.data, read_view.size,
            channel_->get_recv_ring(), // Pass ring buffer pointer for commit
            read_view.read_idx);

        // Mark current operation as complete
        // The proxy will unmarshal from current_buffer, then call
        // commit_read_if_needed()
        (*wq_.front()).on_executed();
        pop_and_execute_next_task();
      };

  // Set up data receive handler
  channel_->on_data_received = [this](std::vector<char>&& data) {
    std::lock_guard lock(mutex_);
    if (wq_.empty()) {
      std::cerr << "SharedMemoryConnection: Received unsolicited response"
                << std::endl;
      return;
    }

    // Validate header size (security check)
    if (data.size() < sizeof(impl::flat::Header)) {
      std::cerr << "SharedMemoryConnection: Message too small: " << data.size()
                << std::endl;
      return;
    }

    auto& current_buffer = current_rx_buffer();
    current_buffer.consume(current_buffer.size());
    auto mb = current_buffer.prepare(data.size());
    std::memcpy(mb.data(), data.data(), data.size());
    current_buffer.commit(data.size());

    // Mark current operation as complete
    (*wq_.front()).on_executed();
    pop_and_execute_next_task();
  };

  start_timeout_timer();
}

SharedMemoryConnection::~SharedMemoryConnection() { close(); }

bool SharedMemoryConnection::prepare_write_buffer(flat_buffer& buffer,
                                                  size_t max_size,
                                                  const EndPoint* endpoint)
{
  if (!channel_)
    return false;

  // std::cout << "prepare_write_buffer called for channel ID: "
  //           << channel_->channel_id() << std::endl;

  auto reservation = channel_->reserve_write(max_size);
  if (!reservation) {
    std::cerr << "SharedMemoryConnection: prepare_write_buffer failed to "
                 "reserve buffer of size "
              << max_size << std::endl;
    return false; // Buffer full
  }

  // Store reservation for later commit (used by send_receive)
  active_reservation_ = reservation;

  // Set buffer to view mode pointing into the ring buffer
  // Pass endpoint and reservation info so the buffer can:
  // 1. Request a larger buffer if needed (using endpoint)
  // 2. Return the reservation for commit (using write_idx)
  buffer.set_view(reservation.data, 0, reservation.max_size, endpoint,
                  reservation.write_idx, true);

  return true;
}

} // namespace nprpc::impl
