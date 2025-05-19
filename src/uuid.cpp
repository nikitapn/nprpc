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
#else
    int shm_fd = -1;
    void* map_region = nullptr;
    pthread_mutex_t* mutex = nullptr;
#endif

    uuid_array& uuid_ref() { 
        return *static_cast<uuid_array*>(map_region); 
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
            sizeof(uuid_array),     // Size: low 32-bits
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
            sizeof(uuid_array));   // Number of bytes to map
        
        if (!map_region) {
            throw std::system_error(GetLastError(), std::system_category(), 
                "MapViewOfFile failed");
        }
#else
        // Create or open shared memory
        shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
        if (shm_fd == -1) {
            throw std::system_error(errno, std::system_category(), 
                "shm_open failed");
        }

        // Set the size
        if (ftruncate(shm_fd, sizeof(uuid_array) + sizeof(pthread_mutex_t)) == -1) {
            throw std::system_error(errno, std::system_category(), 
                "ftruncate failed");
        }

        // Map the shared memory
        map_region = mmap(nullptr, sizeof(uuid_array) + sizeof(pthread_mutex_t), 
            PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        
        if (map_region == MAP_FAILED) {
            throw std::system_error(errno, std::system_category(), 
                "mmap failed");
        }

        // Setup mutex in shared memory
        mutex = static_cast<pthread_mutex_t*>(static_cast<void*>(
            static_cast<char*>(map_region) + sizeof(uuid_array)));

        // Try to initialize mutex (only first process should do this)
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        
        if (pthread_mutex_init(mutex, &attr) == 0) {
            created_new = true;
        }
        
        pthread_mutexattr_destroy(&attr);
        
        // Lock the mutex
        pthread_mutex_lock(mutex);
#endif

        if (created_new) {
            // Generate initial UUID
            generate_new_uuid();
        }

#ifdef _WIN32
        ReleaseMutex(mutex);
#else
        pthread_mutex_unlock(mutex);
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
            munmap(map_region, sizeof(uuid_array) + sizeof(pthread_mutex_t));
        }
        if (shm_fd != -1) {
            close(shm_fd);
            shm_unlink(SHM_NAME);  // Remove shared memory on last process exit
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
        pthread_mutex_lock(mutex);
#endif
    }

    void unlock() {
#ifdef _WIN32
        ReleaseMutex(mutex);
#else
        pthread_mutex_unlock(mutex);
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