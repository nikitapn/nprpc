#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/shared_memory_channel.hpp>
#include <nprpc/common.hpp>
#include <iostream>
#include <future>

namespace nprpc::impl {
void SharedMemoryConnection::add_work(std::unique_ptr<work>&& w) {
    boost::asio::post(ioc_, [w{std::move(w)}, this]() mutable {
        std::lock_guard<std::mutex> lock(mutex_);
        wq_.push_back(std::move(w));
        if (wq_.size() == 1) (*wq_.front())();
    });
}

void SharedMemoryConnection::send_receive(flat_buffer& buffer, uint32_t timeout_ms) {
    assert(*(uint32_t*)buffer.data().data() == buffer.size() - 4);

    if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryMessageContent) {
        dump_message(buffer, false);
    }

    struct work_impl : work {
        flat_buffer& buf;
        SharedMemoryConnection& this_;
        uint32_t timeout_ms;
        std::promise<boost::system::error_code> promise;

        void operator()() noexcept override {
            this_.set_timeout(timeout_ms);
            
            // Send data through shared memory channel
            if (!this_.channel_->send(buf.data().data(), buf.size())) {
                on_failed(boost::system::error_code(
                    boost::asio::error::no_buffer_space,
                    boost::system::system_category()));
                this_.pop_and_execute_next_task();
                return;
            }
        }

        void on_failed(const boost::system::error_code& ec) noexcept override {
            promise.set_value(ec);
        }

        void on_executed() noexcept override {
            promise.set_value({});
        }

        std::future<boost::system::error_code> get_future() {
            return promise.get_future();
        }

        flat_buffer& buffer() noexcept override { return buf; }

        work_impl(flat_buffer& _buf, SharedMemoryConnection& _this_, uint32_t _timeout_ms)
            : buf(_buf)
            , this_(_this_)
            , timeout_ms(_timeout_ms)
        {
        }
    };

    auto post_work_and_wait = [&]() -> boost::system::error_code {
        auto w = std::make_unique<work_impl>(buffer, *this, timeout_ms);
        auto fut = w->get_future();
        add_work(std::move(w));
        return fut.get();
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

    struct work_impl : work {
        flat_buffer buf;
        SharedMemoryConnection& this_;
        uint32_t timeout_ms;
        std::optional<std::function<void(const boost::system::error_code&, flat_buffer&)>> handler;
        
        void operator()() noexcept override {
            this_.set_timeout(timeout_ms);
            
            if (!this_.channel_->send(buf.data().data(), buf.size())) {
                on_failed(boost::system::error_code(
                    boost::asio::error::no_buffer_space,
                    boost::system::system_category()));
                this_.pop_and_execute_next_task();
                return;
            }
        }

        void on_failed(const boost::system::error_code& ec) noexcept override {
            if (handler) handler.value()(ec, buf);
        }

        void on_executed() noexcept override {
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
    remote_endpoint_ = endpoint;
    timeout_timer_.expires_at(boost::posix_time::pos_infin);

    // Create shared memory channel
    /*bool is_server = endpoint.is_local();
    try {
        channel_ = std::make_unique<SharedMemoryChannel>(ioc_, is_server);
    } catch (const std::exception& e) {
        throw nprpc::Exception(
            ("Could not create shared memory channel: "s + e.what()).c_str());
    }*/

    // Set up data receive handler
    channel_->on_data_received = [this](std::vector<char>&& data) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (wq_.empty()) return;  // No pending operations

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

} // namespace nprpc::impl
