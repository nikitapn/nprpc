#pragma once

#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/shared_memory_channel.hpp>
#include <memory>
#include <mutex>
#include <nprpc/impl/misc/mutex.hpp>

namespace nprpc::impl {

/**
 * @brief RPC connection using shared memory transport (Boost.Interprocess)
 * 
 * Similar to SocketConnection but uses message queues instead of TCP.
 * Provides same security guarantees (message size limits, pending request limits).
 */
class SharedMemoryConnection 
    : public Session
    , public CommonConnection<SharedMemoryConnection>
    , public std::enable_shared_from_this<SharedMemoryConnection>
{
    boost::asio::io_context& ioc_;
    std::unique_ptr<SharedMemoryChannel> channel_;
    // SpinMutex mutex_;
    // AdaptiveSpinMutex mutex_;
    std::mutex mutex_;
    uint32_t pending_requests_ = 0;

    friend struct SharedMemoryConnectionWork;

protected:
    virtual void timeout_action() final;

public:
    auto get_executor() noexcept {
        return ioc_.get_executor();
    }

    // Custom add_work with mutex synchronization (can't use base class default)
    template<typename Work>
    void add_work(Work&& w) {
        boost::asio::post(ioc_, [w{std::move(w)}, this]() mutable {
            std::lock_guard lock(mutex_);
            wq_.push_back(std::move(w));
            if (wq_.size() == 1) wq_.front()();
        });
    }

    void send_receive(flat_buffer& buffer, uint32_t timeout_ms) override;
    
    void send_receive_async(
        flat_buffer&& buffer,
        std::optional<std::function<void(const boost::system::error_code&, flat_buffer&)>>&& completion_handler,
        uint32_t timeout_ms) override;

    SharedMemoryConnection(const EndPoint& endpoint, boost::asio::io_context& ioc);
    ~SharedMemoryConnection();
};

} // namespace nprpc::impl
