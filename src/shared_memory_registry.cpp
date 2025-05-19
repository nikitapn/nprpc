#include <nprpc/impl/shared_memory_nameserver.hpp>
#include <boost/uuid/random_generator.hpp>
#include <sstream>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace nprpc::impl {

class SharedMemoryRegistry {
    SharedMemoryNameServer* nameserver_ = nullptr;
#ifdef _WIN32
    HANDLE mapping_ = nullptr;
    HANDLE mutex_ = nullptr;
#else
    int fd_ = -1;
#endif
    bool is_server_;

public:
    // Generate unique channel names for a new connection
    static std::string generate_channel_names(uint32_t channel_id) {
        auto uuid = boost::uuids::random_generator()();
        std::stringstream ss;
        ss << "mem://" << std::hex << uuid << "/" << channel_id;
        return ss.str();
    }

    // Client calls this to request a new channel
    uint32_t request_channel() {
        if (is_server_) return -1;

        lock();
        uint32_t channel_id = -1;
        
        // Find free channel
        for (uint32_t i = 0; i < SharedMemoryNameServer::MAX_CHANNELS; ++i) {
            if (!nameserver_->channels[i].in_use) {
                channel_id = i;
                auto& ch = nameserver_->channels[i];
                
                // Generate unique names for this channel
                std::string base_name = generate_channel_names(i);
                snprintf(ch.s2c_name.data(), ch.s2c_name.size(), 
                    "%s_s2c", base_name.c_str());
                snprintf(ch.c2s_name.data(), ch.c2s_name.size(), 
                    "%s_c2s", base_name.c_str());
                snprintf(ch.event_read.data(), ch.event_read.size(), 
                    "%s_read", base_name.c_str());
                snprintf(ch.event_write.data(), ch.event_write.size(), 
                    "%s_write", base_name.c_str());
                
                ch.in_use = true;
                ch.last_heartbeat = std::chrono::system_clock::now()
                    .time_since_epoch().count();
                break;
            }
        }

        unlock();
        return channel_id;
    }

    // Server calls this to check for new channel requests
    bool get_channel_info(uint32_t channel_id, 
        std::string& s2c, std::string& c2s, 
        std::string& evt_read, std::string& evt_write) 
    {
        if (!is_server_) return false;

        lock();
        if (channel_id >= SharedMemoryNameServer::MAX_CHANNELS || 
            !nameserver_->channels[channel_id].in_use) {
            unlock();
            return false;
        }

        auto& ch = nameserver_->channels[channel_id];
        s2c = ch.s2c_name.data();
        c2s = ch.c2s_name.data();
        evt_read = ch.event_read.data();
        evt_write = ch.event_write.data();
        unlock();
        return true;
    }

    // Client/Server heartbeat
    void update_heartbeat(uint32_t channel_id) {
        if (channel_id >= SharedMemoryNameServer::MAX_CHANNELS) return;
        
        lock();
        auto& ch = nameserver_->channels[channel_id];
        if (ch.in_use) {
            ch.last_heartbeat = std::chrono::system_clock::now()
                .time_since_epoch().count();
        }
        unlock();
    }

    // Server cleanup of dead channels
    void cleanup_dead_channels(uint32_t timeout_ms) {
        if (!is_server_) return;

        lock();
        auto now = std::chrono::system_clock::now()
            .time_since_epoch().count();
        
        for (uint32_t i = 0; i < SharedMemoryNameServer::MAX_CHANNELS; ++i) {
            auto& ch = nameserver_->channels[i];
            if (ch.in_use) {
                auto diff = now - ch.last_heartbeat;
                if (diff > timeout_ms * 1000000) { // Convert ms to ns
                    ch.in_use = false;
                }
            }
        }
        unlock();
    }

    SharedMemoryRegistry(bool is_server) : is_server_(is_server) {
#ifdef _WIN32
        mapping_ = is_server ? 
            CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                0, sizeof(SharedMemoryNameServer), 
                SharedMemoryNameServer::NAMESERVER_NAME) :
            OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, 
                SharedMemoryNameServer::NAMESERVER_NAME);

        if (!mapping_) {
            throw std::runtime_error("Failed to create/open nameserver mapping");
        }

        nameserver_ = static_cast<SharedMemoryNameServer*>(
            MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, 0));

        if (!nameserver_) {
            CloseHandle(mapping_);
            throw std::runtime_error("Failed to map nameserver view");
        }

        if (is_server) {
            // Initialize nameserver
            nameserver_->magic = SharedMemoryNameServer::MAGIC;
            nameserver_->server_pid = GetCurrentProcessId();
            nameserver_->channel_count = 0;
            nameserver_->server_ready = true;
            sprintf_s(nameserver_->mutex_name, sizeof(nameserver_->mutex_name),
                "Local\\nprpc_ns_mutex");
        } else {
            // Wait for server to be ready
            while (!nameserver_->server_ready) {
                Sleep(100);
            }
        }

        // Open/create mutex
        mutex_ = CreateMutexA(nullptr, FALSE, nameserver_->mutex_name);
        if (!mutex_) {
            UnmapViewOfFile(nameserver_);
            CloseHandle(mapping_);
            throw std::runtime_error("Failed to create/open mutex");
        }
#else
        fd_ = is_server ?
            shm_open(SharedMemoryNameServer::NAMESERVER_NAME, 
                O_CREAT | O_RDWR, S_IRUSR | S_IWUSR) :
            shm_open(SharedMemoryNameServer::NAMESERVER_NAME, 
                O_RDWR, S_IRUSR | S_IWUSR);

        if (fd_ == -1) {
            throw std::runtime_error("Failed to create/open nameserver");
        }

        if (is_server) {
            if (ftruncate(fd_, sizeof(SharedMemoryNameServer)) == -1) {
                close(fd_);
                throw std::runtime_error("Failed to set nameserver size");
            }
        }

        nameserver_ = static_cast<SharedMemoryNameServer*>(mmap(
            nullptr, sizeof(SharedMemoryNameServer), 
            PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0));

        if (nameserver_ == MAP_FAILED) {
            close(fd_);
            throw std::runtime_error("Failed to map nameserver");
        }

        if (is_server) {
            // Initialize nameserver
            nameserver_->magic = SharedMemoryNameServer::MAGIC;
            nameserver_->server_pid = getpid();
            nameserver_->channel_count = 0;
            
            // Initialize mutex
            pthread_mutexattr_t attr;
            pthread_mutexattr_init(&attr);
            pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
            pthread_mutex_init(
                reinterpret_cast<pthread_mutex_t*>(&nameserver_->mutex), 
                &attr);
            pthread_mutexattr_destroy(&attr);
            
            nameserver_->server_ready = true;
        } else {
            // Wait for server to be ready
            while (!nameserver_->server_ready) {
                usleep(100000);
            }
        }
#endif
    }

    ~SharedMemoryRegistry() {
#ifdef _WIN32
        if (nameserver_) UnmapViewOfFile(nameserver_);
        if (mapping_) CloseHandle(mapping_);
        if (mutex_) CloseHandle(mutex_);
#else
        if (nameserver_ != MAP_FAILED && nameserver_ != nullptr) {
            if (is_server_) {
                // Cleanup mutex
                pthread_mutex_destroy(
                    reinterpret_cast<pthread_mutex_t*>(&nameserver_->mutex));
            }
            munmap(nameserver_, sizeof(SharedMemoryNameServer));
        }
        if (fd_ != -1) {
            close(fd_);
            if (is_server_) {
                shm_unlink(SharedMemoryNameServer::NAMESERVER_NAME);
            }
        }
#endif
    }

private:
    void lock() {
#ifdef _WIN32
        WaitForSingleObject(mutex_, INFINITE);
#else
        pthread_mutex_lock(
            reinterpret_cast<pthread_mutex_t*>(&nameserver_->mutex));
#endif
    }

    void unlock() {
#ifdef _WIN32
        ReleaseMutex(mutex_);
#else
        pthread_mutex_unlock(
            reinterpret_cast<pthread_mutex_t*>(&nameserver_->mutex));
#endif
    }
};
