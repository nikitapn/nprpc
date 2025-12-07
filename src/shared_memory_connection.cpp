#include <nprpc/impl/shared_memory_connection.hpp>
#include <nprpc/impl/shared_memory_listener.hpp>
#include <nprpc/common.hpp>
#include <iostream>
#include <future>

namespace nprpc::impl {

void SharedMemoryConnection::timeout_action() {
    // For shared memory, timeout means the other side is unresponsive
    // std::cerr << "SharedMemoryConnection timeout" << std::endl;
    // close();
}

void SharedMemoryConnection::add_work(std::unique_ptr<IOWork>&& w) {
    boost::asio::post(ioc_, [w{std::move(w)}, this]() mutable {
        std::lock_guard lock(mutex_);
        wq_.push_back(std::move(w));
        if (wq_.size() == 1) (*wq_.front())();
    });
}

void SharedMemoryConnection::send_receive(flat_buffer& buffer, uint32_t timeout_ms) {
    assert(*(uint32_t*)buffer.data().data() == buffer.size() - 4);

    if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryMessageContent) {
        dump_message(buffer, false);
    }

    struct work_impl : IOWork {
        flat_buffer& buf;
        SharedMemoryConnection& this_;
        uint32_t timeout_ms;
        LockFreeRingBuffer::WriteReservation reservation;  // For zero-copy write

        std::mutex mtx;
        std::condition_variable cv;
        bool done = false;
        boost::system::error_code result;

        void operator()() noexcept override {
            this_.set_timeout(timeout_ms);

            // Validate message size (security check)
            if (buf.size() > max_message_size) {
                std::cerr << "SharedMemoryConnection: Message too large: " << buf.size() << std::endl;
                on_failed(boost::system::error_code(
                    boost::asio::error::message_size,
                    boost::system::system_category()));
                this_.pop_and_execute_next_task();
                return;
            }

            // Check if this is a zero-copy write (buffer in view mode with active reservation)
            if (buf.is_view_mode() && reservation.valid) {
                // Zero-copy path: data is already in ring buffer, just commit
                this_.channel_->commit_write(reservation, buf.size());
            } else {
                // Normal path: copy data to ring buffer
                if (!this_.channel_->send(buf.data().data(), buf.size())) {
                    on_failed(boost::system::error_code(
                        boost::asio::error::no_buffer_space,
                        boost::system::system_category()));
                    this_.pop_and_execute_next_task();
                    return;
                }
            }
        }

        void on_failed(const boost::system::error_code& ec) noexcept override {
            {
                std::lock_guard<std::mutex> lock(mtx);
                result = ec;
                done = true;
            }
            cv.notify_one();
        }

        void on_executed() noexcept override {
            {
                std::lock_guard<std::mutex> lock(mtx);
                result = boost::system::error_code{};
                done = true;
            }
            cv.notify_one();
        }

        boost::system::error_code wait() {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, [this]{ return done; });
            return result;
        }

        flat_buffer& buffer() noexcept override { return buf; }

        work_impl(flat_buffer& _buf, SharedMemoryConnection& _this_, uint32_t _timeout_ms,
                  LockFreeRingBuffer::WriteReservation _reservation)
            : buf(_buf)
            , this_(_this_)
            , timeout_ms(_timeout_ms)
            , reservation(_reservation)
        {
        }
    };

    auto post_work_and_wait = [&]() -> boost::system::error_code {
        // Transfer reservation from connection to work item
        auto reservation = std::exchange(active_reservation_, {});
        auto w = std::make_unique<work_impl>(buffer, *this, timeout_ms, reservation);
        auto* w_ptr = w.get();
        add_work(std::move(w));
        return w_ptr->wait();
    };

    auto ec = post_work_and_wait();

    if (!ec) {
        if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryMessageContent) {
            dump_message(buffer, true);
        }
        return;
    }

    fail(ec, "send_receive");
    close();
    throw nprpc::ExceptionCommFailure();
}

void SharedMemoryConnection::send_receive_async(
    flat_buffer&& buffer,
    std::optional<std::function<void(const boost::system::error_code&, flat_buffer&)>>&& completion_handler,
    uint32_t timeout_ms)
{
    assert(*(uint32_t*)buffer.data().data() == buffer.size() - 4);

    pending_requests_++;

    struct work_impl : IOWork {
        flat_buffer buf;
        SharedMemoryConnection& this_;
        uint32_t timeout_ms;
        std::optional<std::function<void(const boost::system::error_code&, flat_buffer&)>> handler;

        void operator()() noexcept override {
            this_.set_timeout(timeout_ms);

            // Validate message size (security check)
            if (buf.size() > max_message_size) {
                std::cerr << "SharedMemoryConnection: Message too large: " << buf.size() << std::endl;
                on_failed(boost::system::error_code(
                    boost::asio::error::message_size,
                    boost::system::system_category()));
                this_.pop_and_execute_next_task();
                return;
            }

            if (!this_.channel_->send(buf.data().data(), buf.size())) {
                on_failed(boost::system::error_code(
                    boost::asio::error::no_buffer_space,
                    boost::system::system_category()));
                this_.pop_and_execute_next_task();
                return;
            }
        }

        void on_failed(const boost::system::error_code& ec) noexcept override {
            {
                std::lock_guard lock(this_.mutex_);
                this_.pending_requests_--;
            }
            if (handler) handler.value()(ec, buf);
        }

        void on_executed() noexcept override {
            {
                std::lock_guard lock(this_.mutex_);
                this_.pending_requests_--;
            }
            if (handler) handler.value()(boost::system::error_code{}, buf);
        }

        flat_buffer& buffer() noexcept override { return buf; }

        work_impl(flat_buffer&& _buf, 
            SharedMemoryConnection& _this_, 
            std::optional<std::function<void(const boost::system::error_code&, flat_buffer&)>>&& _handler,
            uint32_t _timeout_ms)
            : buf(std::move(_buf))
            , this_(_this_)
            , timeout_ms(_timeout_ms)
            , handler(std::move(_handler))
        {
        }
    };

    add_work(std::make_unique<work_impl>(
        std::move(buffer), *this, std::move(completion_handler), timeout_ms));
}

SharedMemoryConnection::SharedMemoryConnection(const EndPoint& endpoint, boost::asio::io_context& ioc)
    : Session(ioc.get_executor())
    , ioc_(ioc)
{
    ctx_.remote_endpoint = endpoint;
    timeout_timer_.expires_at(boost::posix_time::pos_infin);

    // Parse endpoint to extract listener name
    // Expected format: mem://listener_name (no port needed for shared memory)
    std::string listener_name(endpoint.memory_channel_id());
    if (listener_name.empty()) {
        throw nprpc::Exception("Invalid shared memory endpoint: missing listener name");
    }

    // Connect to the listener, which will establish a dedicated channel
    try {
        channel_ = connect_to_shared_memory_listener(ioc_, listener_name);
    } catch (const std::exception& e) {
        std::string error_msg = "Could not connect to shared memory listener: ";
        error_msg += e.what();
        throw nprpc::Exception(error_msg.c_str());
    }

    // Set up data receive handler
    channel_->on_data_received = [this](std::vector<char>&& data) {
        std::lock_guard lock(mutex_);
        if (wq_.empty()) {
            std::cerr << "SharedMemoryConnection: Received unsolicited response" << std::endl;
            return;
        }

        // Validate header size (security check)
        if (data.size() < sizeof(impl::flat::Header)) {
            std::cerr << "SharedMemoryConnection: Message too small: " << data.size() << std::endl;
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

SharedMemoryConnection::~SharedMemoryConnection() {
    close();
}

bool SharedMemoryConnection::prepare_write_buffer(flat_buffer& buffer, size_t max_size) {
    if (!channel_) return false;

    auto reservation = channel_->reserve_write(max_size);
    if (!reservation) {
        return false;  // Buffer full
    }

    // Store reservation for later commit
    active_reservation_ = reservation;

    // Set buffer to view mode pointing into the ring buffer
    buffer.set_view(reinterpret_cast<char*>(reservation.data), 0, reservation.max_size);

    return true;
}

} // namespace nprpc::impl
