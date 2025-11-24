#include <nprpc/impl/shared_memory_connection.hpp>
#include <nprpc/impl/shared_memory_listener.hpp>
#include <nprpc/common.hpp>
#include <iostream>
#include <condition_variable>

namespace nprpc::impl {

// SharedMemoryConnectionWork implementation

// Sync constructor
SharedMemoryConnectionWork::SharedMemoryConnectionWork(
    flat_buffer& buf, SharedMemoryConnection& conn, uint32_t timeout,
    std::mutex& mtx, std::condition_variable& cv, bool& done, boost::system::error_code& result)
    : buf_ptr(&buf)
    , conn_ptr(&conn)
    , timeout_ms(timeout)
    , mtx_ptr(&mtx)
    , cv_ptr(&cv)
    , done_ptr(&done)
    , result_ptr(&result)
{
}

// Async constructor
SharedMemoryConnectionWork::SharedMemoryConnectionWork(
    flat_buffer&& buf, std::shared_ptr<SharedMemoryConnection> conn, uint32_t timeout,
    std::optional<std::function<void(const boost::system::error_code&, flat_buffer&)>>&& handler)
    : buf_storage(std::move(buf))
    , buf_ptr(&buf_storage)
    , conn_shared(std::move(conn))
    , conn_ptr(conn_shared.get())
    , timeout_ms(timeout)
    , async_handler(std::move(handler))
{
}

void SharedMemoryConnectionWork::operator()() noexcept {
    conn_ptr->set_timeout(timeout_ms);
    
    // Validate message size (security check)
    if (buf_ptr->size() > max_message_size) {
        std::cerr << "SharedMemoryConnection: Message too large: " << buf_ptr->size() << std::endl;
        on_failed(boost::system::error_code(
            boost::asio::error::message_size,
            boost::system::system_category()));
        conn_ptr->pop_and_execute_next_task();
        return;
    }
    
    // Send data through shared memory channel
    if (!conn_ptr->channel_->send(buf_ptr->data().data(), buf_ptr->size())) {
        on_failed(boost::system::error_code(
            boost::asio::error::no_buffer_space,
            boost::system::system_category()));
        conn_ptr->pop_and_execute_next_task();
        return;
    }
}

void SharedMemoryConnectionWork::on_failed(const boost::system::error_code& ec) noexcept {
    if (async_handler) {
        // Async path
        {
            std::lock_guard lock(conn_ptr->mutex_);
            conn_ptr->pending_requests_--;
        }
        async_handler.value()(ec, *buf_ptr);
    } else {
        // Sync path
        {
            std::lock_guard<std::mutex> lock(*mtx_ptr);
            *result_ptr = ec;
            *done_ptr = true;
        }
        cv_ptr->notify_one();
    }
}

void SharedMemoryConnectionWork::on_executed() noexcept {
    if (async_handler) {
        // Async path
        {
            std::lock_guard lock(conn_ptr->mutex_);
            conn_ptr->pending_requests_--;
        }
        async_handler.value()(boost::system::error_code{}, *buf_ptr);
    } else {
        // Sync path
        {
            std::lock_guard<std::mutex> lock(*mtx_ptr);
            *result_ptr = boost::system::error_code{};
            *done_ptr = true;
        }
        cv_ptr->notify_one();
    }
}

void SharedMemoryConnection::timeout_action() {
    // For shared memory, timeout means the other side is unresponsive
    // std::cerr << "SharedMemoryConnection timeout" << std::endl;
    // close();
}

void SharedMemoryConnection::send_receive(flat_buffer& buffer, uint32_t timeout_ms) {
    assert(*(uint32_t*)buffer.data().data() == buffer.size() - 4);

    if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryMessageContent) {
        dump_message(buffer, false);
    }

    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    boost::system::error_code result;

    add_work(SharedMemoryConnectionWork(buffer, *this, timeout_ms, mtx, cv, done, result));

    // Wait for completion
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [&done]{ return done; });

    if (!result) {
        if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryMessageContent) {
            dump_message(buffer, true);
        }
        return;
    }

    fail(result, "send_receive");
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

    add_work(SharedMemoryConnectionWork(
        std::move(buffer), shared_from_this(), timeout_ms, std::move(completion_handler)));
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
        wq_.front().on_executed();
        pop_and_execute_next_task();
    };

    start_timeout_timer();
}

SharedMemoryConnection::~SharedMemoryConnection() {
    close();
}

} // namespace nprpc::impl
