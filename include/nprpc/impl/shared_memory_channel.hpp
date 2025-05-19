#pragma once

#include <nprpc/impl/shared_ring_buffer.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <boost/asio.hpp>
#include <iostream>
#include <future>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <semaphore.h>
#endif


namespace nprpc::impl {

class SharedMemoryChannel {
    std::string s2c_name_;
    std::string c2s_name_;
    std::string event_read_name_;
    std::string event_write_name_;
    uint32_t channel_id_;

#ifdef _WIN32
    HANDLE mapping_s2c_ = nullptr;
    HANDLE mapping_c2s_ = nullptr;
    HANDLE event_read_ = nullptr;
    HANDLE event_write_ = nullptr;
#else
    int fd_s2c_ = -1;
    int fd_c2s_ = -1;
    sem_t* sem_read_ = nullptr;
    sem_t* sem_write_ = nullptr;
#endif

    SharedRingBuffer* buffer_s2c_ = nullptr;
    SharedRingBuffer* buffer_c2s_ = nullptr;
    bool is_server_;
    
    boost::asio::io_context& ioc_;
    std::unique_ptr<std::thread> poll_thread_;
    bool running_ = true;

public:
    NPRPC_API SharedMemoryChannel(boost::asio::io_context& ioc, bool is_server);
    NPRPC_API ~SharedMemoryChannel();

    NPRPC_API bool send(const void* data, uint32_t size);
    std::function<void(std::vector<char>&&)> on_data_received;
};

} // namespace nprpc::impl
