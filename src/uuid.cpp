// Copyright (c) 2021-2025 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by
// LICENSING file in the topmost directory

#include <nprpc/impl/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid.hpp>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#endif

#include <system_error>

namespace nprpc::impl {

struct SharedUUID::Impl {
    static constexpr const char* SHM_NAME = 
#ifdef _WIN32
        "Local\\nprpc_uuid";
#else
        "/nprpc_uuid";
#endif

    static constexpr const char* MUTEX_NAME = 
#ifdef _WIN32
        "Local\\nprpc_uuid_mutex";
#else
        "/nprpc_uuid_mutex";
#endif

#ifdef _WIN32
    HANDLE file_mapping = nullptr;
    HANDLE mutex = nullptr;
    void* map_region = nullptr;

    struct Data {
        std::atomic_int ref_cnt;
        uuid_array uuid;
    };
#else
    int shm_fd = -1;
    void* map_region = nullptr;
    struct Data {
        pthread_mutex_t mutex;
        std::atomic_int ref_cnt;
        uuid_array uuid;
    };
#endif

    Data* data() {
        return static_cast<Data*>(map_region);
    }

    uuid_array& uuid_ref() { 
        return data()->uuid;
    }

    Impl() {
        bool created_new = false;

#ifdef _WIN32
        // Try to create mutex first
        mutex = CreateMutexA(nullptr, FALSE, MUTEX_NAME);
        if (!mutex) {
            throw std::system_error(GetLastError(), std::system_category(), 
                "CreateMutex failed");
        }

        WaitForSingleObject(mutex, INFINITE);  // Lock mutex
        
        // Try to create or open shared memory
        file_mapping = CreateFileMappingA(
            INVALID_HANDLE_VALUE,    // Use paging file
            nullptr,                 // Default security
            PAGE_READWRITE,         // Read/write access
            0,                      // Size: high 32-bits
            sizeof(Data),           // Size: low 32-bits
            SHM_NAME);             // Name of mapping object
        
        if (!file_mapping) {
            throw std::system_error(GetLastError(), std::system_category(), 
                "CreateFileMapping failed");
        }

        created_new = (GetLastError() != ERROR_ALREADY_EXISTS);
        
        // Map view of file
        map_region = MapViewOfFile(
            file_mapping,           // Handle to mapping object
            FILE_MAP_ALL_ACCESS,    // Read/write permission
            0, 0,                  // Offset
            sizeof(Data));   // Number of bytes to map
        
        if (!map_region) {
            throw std::system_error(GetLastError(), std::system_category(), 
                "MapViewOfFile failed");
        }
#else
        shm_fd = shm_open(SHM_NAME, O_RDWR, S_IRUSR | S_IWUSR);
        if (shm_fd == -1) {
            assert(errno == ENOENT);
            created_new = true;
            // Create or open shared memory
            shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
            if (shm_fd == -1) {
                throw std::system_error(errno, std::system_category(), 
                    "shm_open failed");
            }
        }

        // Set the size
        if (ftruncate(shm_fd, sizeof(Data)) == -1) {
            throw std::system_error(errno, std::system_category(), 
                "ftruncate failed");
        }

        // Map the shared memory
        map_region = mmap(nullptr, sizeof(Data),
            PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        
        if (map_region == MAP_FAILED) {
            throw std::system_error(errno, std::system_category(), 
                "mmap failed");
        }

        if (created_new) {
            // Try to initialize mutex (only first process should do this)
            pthread_mutexattr_t attr;
            pthread_mutexattr_init(&attr);
            pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
            pthread_mutex_init(&data()->mutex, &attr);
            pthread_mutexattr_destroy(&attr);
        }

        pthread_mutex_lock(&data()->mutex);
#endif

        if (created_new) {
            generate_new_uuid();
            data()->ref_cnt.store(1, std::memory_order_relaxed);
        } else {
            data()->ref_cnt.fetch_add(1, std::memory_order_relaxed);
        }

#ifdef _WIN32
        ReleaseMutex(mutex);
#else
        pthread_mutex_unlock(&data()->mutex);
#endif
    }

    ~Impl() {
#ifdef _WIN32
        if (map_region) {
            UnmapViewOfFile(map_region);
        }
        if (file_mapping) {
            CloseHandle(file_mapping);
        }
        if (mutex) {
            CloseHandle(mutex);
        }
#else
        if (map_region != MAP_FAILED && map_region != nullptr) {
            // Crash recovery is not implemented, so we just decrement ref count
            auto cnt = data()->ref_cnt.fetch_sub(1, std::memory_order_relaxed);
            if (cnt == 1) {
                // Last reference, cleanup
                pthread_mutex_destroy(&data()->mutex);
                // Unlink shared memory
                shm_unlink(SHM_NAME);
            }
            munmap(map_region, sizeof(Data));
        }
        if (shm_fd != -1) {
            close(shm_fd);
        }
#endif
    }

    void generate_new_uuid() {
        auto& uuid = uuid_ref();
        auto new_uuid = boost::uuids::random_generator()();
        std::copy(new_uuid.begin(), new_uuid.end(), uuid.begin());
    }

    void lock() {
#ifdef _WIN32
        WaitForSingleObject(mutex, INFINITE);
#else
        pthread_mutex_lock(&data()->mutex);
#endif
    }

    void unlock() {
#ifdef _WIN32
        ReleaseMutex(mutex);
#else
        pthread_mutex_unlock(&data()->mutex);
#endif
    }
};

SharedUUID& SharedUUID::instance() {
    static SharedUUID instance;
    return instance;
}

SharedUUID::SharedUUID() : pimpl_(std::make_unique<Impl>()) {}
SharedUUID::~SharedUUID() = default;

const SharedUUID::uuid_array& SharedUUID::get() const noexcept {
    return pimpl_->uuid_ref();
}

void SharedUUID::generate_new() {
    pimpl_->lock();
    pimpl_->generate_new_uuid();
    pimpl_->unlock();
}

} // namespace nprpc::impl