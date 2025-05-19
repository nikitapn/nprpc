#include <nprpc/impl/shared_memory_channel.hpp>

namespace nprpc::impl {

NPRPC_API SharedMemoryChannel::SharedMemoryChannel(
  boost::asio::io_context& ioc, bool is_server) 
    : is_server_(is_server)
    , ioc_(ioc) 
{
    std::string event_name = std::string(EVENT_PREFIX) + 
        (is_server ? "s" : "c") + std::to_string(getpid());

#ifdef _WIN32
    if (is_server) {
        // Server creates both mappings
        mapping_s2c_ = CreateFileMappingA(
            INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
            0, sizeof(SharedRingBuffer), SERVER_TO_CLIENT_NAME);
        
        mapping_c2s_ = CreateFileMappingA(
            INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
            0, sizeof(SharedRingBuffer), CLIENT_TO_SERVER_NAME);

        if (!mapping_s2c_ || !mapping_c2s_) {
            throw std::runtime_error("Failed to create shared memory");
        }

        buffer_s2c_ = static_cast<SharedRingBuffer*>(
            MapViewOfFile(mapping_s2c_, FILE_MAP_ALL_ACCESS, 0, 0, 0));
        buffer_c2s_ = static_cast<SharedRingBuffer*>(
            MapViewOfFile(mapping_c2s_, FILE_MAP_ALL_ACCESS, 0, 0, 0));

        // Initialize as empty ring buffers
        new (buffer_s2c_) SharedRingBuffer();
        new (buffer_c2s_) SharedRingBuffer();
        
        buffer_s2c_->header.magic = SharedRingBufferHeader::MAGIC;
        buffer_c2s_->header.magic = SharedRingBufferHeader::MAGIC;
    } else {
        // Client opens existing mappings
        mapping_s2c_ = OpenFileMappingA(
            FILE_MAP_ALL_ACCESS, FALSE, SERVER_TO_CLIENT_NAME);
        mapping_c2s_ = OpenFileMappingA(
            FILE_MAP_ALL_ACCESS, FALSE, CLIENT_TO_SERVER_NAME);

        if (!mapping_s2c_ || !mapping_c2s_) {
            throw std::runtime_error("Failed to open shared memory");
        }

        buffer_s2c_ = static_cast<SharedRingBuffer*>(
            MapViewOfFile(mapping_s2c_, FILE_MAP_ALL_ACCESS, 0, 0, 0));
        buffer_c2s_ = static_cast<SharedRingBuffer*>(
            MapViewOfFile(mapping_c2s_, FILE_MAP_ALL_ACCESS, 0, 0, 0));
    }

    // Create/open events for signaling
    event_read_ = CreateEventA(nullptr, FALSE, FALSE, 
        (event_name + "_r").c_str());
    event_write_ = CreateEventA(nullptr, FALSE, FALSE, 
        (event_name + "_w").c_str());

#else
    if (is_server) {
        // Server creates both shared memory segments
        fd_s2c_ = shm_open(SERVER_TO_CLIENT_NAME, 
            O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
        fd_c2s_ = shm_open(CLIENT_TO_SERVER_NAME, 
            O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);

        if (fd_s2c_ == -1 || fd_c2s_ == -1) {
            throw std::runtime_error("Failed to create shared memory");
        }

        ftruncate(fd_s2c_, sizeof(SharedRingBuffer));
        ftruncate(fd_c2s_, sizeof(SharedRingBuffer));

        buffer_s2c_ = static_cast<SharedRingBuffer*>(mmap(
            nullptr, sizeof(SharedRingBuffer), 
            PROT_READ | PROT_WRITE, MAP_SHARED, fd_s2c_, 0));
        buffer_c2s_ = static_cast<SharedRingBuffer*>(mmap(
            nullptr, sizeof(SharedRingBuffer), 
            PROT_READ | PROT_WRITE, MAP_SHARED, fd_c2s_, 0));

        // Initialize as empty ring buffers
        new (buffer_s2c_) SharedRingBuffer();
        new (buffer_c2s_) SharedRingBuffer();
        
        buffer_s2c_->header.magic = SharedRingBufferHeader::MAGIC;
        buffer_c2s_->header.magic = SharedRingBufferHeader::MAGIC;
    } else {
        // Client opens existing shared memory
        fd_s2c_ = shm_open(SERVER_TO_CLIENT_NAME, O_RDWR, S_IRUSR | S_IWUSR);
        fd_c2s_ = shm_open(CLIENT_TO_SERVER_NAME, O_RDWR, S_IRUSR | S_IWUSR);

        if (fd_s2c_ == -1 || fd_c2s_ == -1) {
            throw std::runtime_error("Failed to open shared memory");
        }

        buffer_s2c_ = static_cast<SharedRingBuffer*>(mmap(
            nullptr, sizeof(SharedRingBuffer), 
            PROT_READ | PROT_WRITE, MAP_SHARED, fd_s2c_, 0));
        buffer_c2s_ = static_cast<SharedRingBuffer*>(mmap(
            nullptr, sizeof(SharedRingBuffer), 
            PROT_READ | PROT_WRITE, MAP_SHARED, fd_c2s_, 0));
    }

    // Create/open semaphores for signaling
    sem_read_ = sem_open((event_name + "_r").c_str(), 
        O_CREAT, S_IRUSR | S_IWUSR, 0);
    sem_write_ = sem_open((event_name + "_w").c_str(), 
        O_CREAT, S_IRUSR | S_IWUSR, 0);
#endif

    if (buffer_s2c_->header.magic != SharedRingBufferHeader::MAGIC ||
        buffer_c2s_->header.magic != SharedRingBufferHeader::MAGIC) {
        throw std::runtime_error("Invalid shared memory format");
    }

    // Start polling thread
    poll_thread_ = std::make_unique<std::thread>([this]() {
        while (running_) {
#ifdef _WIN32
            WaitForSingleObject(event_read_, 100); // 100ms timeout
#else
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += 100000000; // 100ms
            sem_timedwait(sem_read_, &ts);
#endif
            if (!running_) break;

            SharedRingBuffer* read_buf = 
                is_server_ ? buffer_c2s_ : buffer_s2c_;

            uint32_t size;
            std::vector<char> data(SharedRingBufferHeader::BUFFER_SIZE);

            while (read_buf->try_read(data.data(), size)) {
                // Post received data to io_context
                boost::asio::post(ioc_, [this, data = std::vector<char>(
                    data.begin(), data.begin() + size)]() mutable {
                    on_data_received(std::move(data));
                });
            }
        }
    });
}

NPRPC_API SharedMemoryChannel::~SharedMemoryChannel() {
    running_ = false;
    if (poll_thread_) {
        poll_thread_->join();
    }

#ifdef _WIN32
    if (buffer_s2c_) UnmapViewOfFile(buffer_s2c_);
    if (buffer_c2s_) UnmapViewOfFile(buffer_c2s_);
    if (mapping_s2c_) CloseHandle(mapping_s2c_);
    if (mapping_c2s_) CloseHandle(mapping_c2s_);
    if (event_read_) CloseHandle(event_read_);
    if (event_write_) CloseHandle(event_write_);
#else
    if (buffer_s2c_) munmap(buffer_s2c_, sizeof(SharedRingBuffer));
    if (buffer_c2s_) munmap(buffer_c2s_, sizeof(SharedRingBuffer));
    if (fd_s2c_ != -1) {
        close(fd_s2c_);
        if (is_server_) shm_unlink(SERVER_TO_CLIENT_NAME);
    }
    if (fd_c2s_ != -1) {
        close(fd_c2s_);
        if (is_server_) shm_unlink(CLIENT_TO_SERVER_NAME);
    }
    if (sem_read_) {
        sem_close(sem_read_);
        if (is_server_) {
            std::string name = std::string(EVENT_PREFIX) + "s" + 
                std::to_string(getpid()) + "_r";
            sem_unlink(name.c_str());
        }
    }
    if (sem_write_) {
        sem_close(sem_write_);
        if (is_server_) {
            std::string name = std::string(EVENT_PREFIX) + "s" + 
                std::to_string(getpid()) + "_w";
            sem_unlink(name.c_str());
        }
    }
#endif
}

NPRPC_API bool SharedMemoryChannel::send(const void* data, uint32_t size) {
    SharedRingBuffer* write_buf = 
        is_server_ ? buffer_s2c_ : buffer_c2s_;

    if (!write_buf->try_write(data, size)) {
        return false;
    }

#ifdef _WIN32
    SetEvent(event_write_);
#else
    sem_post(sem_write_);
#endif
    return true;
}

} // namespace nprpc::impl
