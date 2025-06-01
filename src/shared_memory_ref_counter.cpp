#include <nprpc/impl/shared_memory_ref_counter.hpp>
#include <stdexcept>
#include <iostream>

namespace nprpc::impl {

SharedMemoryRefCounter::SharedMemoryRefCounter(const std::string& shm_name, 
                                             CleanupFunction cleanup_func)
    : shm_name_(shm_name)
    , cleanup_func_(cleanup_func) {
    
    bool created_new = false;
    
#ifdef _WIN32
    std::string mutex_name = shm_name + "_mutex";
    
    // Try to open existing mutex first
    mutex_ = OpenMutex(MUTEX_ALL_ACCESS, FALSE, mutex_name.c_str());
    if (!mutex_) {
        // Create new mutex
        mutex_ = CreateMutex(nullptr, FALSE, mutex_name.c_str());
        if (!mutex_) {
            throw std::system_error(GetLastError(), std::system_category(),
                "CreateMutex failed");
        }
        created_new = true;
    }
    
    // Try to open existing file mapping
    file_mapping_ = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, shm_name.c_str());
    if (!file_mapping_) {
        // Create new file mapping
        file_mapping_ = CreateFileMapping(INVALID_HANDLE_VALUE, nullptr,
            PAGE_READWRITE, 0, sizeof(CounterData), shm_name.c_str());
        if (!file_mapping_) {
            CloseHandle(mutex_);
            throw std::system_error(GetLastError(), std::system_category(),
                "CreateFileMapping failed");
        }
        created_new = true;
    }
    
    counter_data_ = static_cast<CounterData*>(MapViewOfFile(file_mapping_,
        FILE_MAP_ALL_ACCESS, 0, 0, sizeof(CounterData)));
    
    if (!counter_data_) {
        CloseHandle(file_mapping_);
        CloseHandle(mutex_);
        throw std::system_error(GetLastError(), std::system_category(),
            "MapViewOfFile failed");
    }
#else
    // Try to open existing shared memory
    shm_fd_ = shm_open(shm_name.c_str(), O_RDWR, S_IRUSR | S_IWUSR);
    if (shm_fd_ == -1) {
        if (errno != ENOENT) {
            throw std::system_error(errno, std::system_category(),
                "shm_open failed");
        }
        
        // Create new shared memory
        created_new = true;
        shm_fd_ = shm_open(shm_name.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
        if (shm_fd_ == -1) {
            throw std::system_error(errno, std::system_category(),
                "shm_open failed to create");
        }
        
        if (ftruncate(shm_fd_, sizeof(CounterData)) == -1) {
            close(shm_fd_);
            shm_unlink(shm_name.c_str());
            throw std::system_error(errno, std::system_category(),
                "ftruncate failed");
        }
    }
    
    counter_data_ = static_cast<CounterData*>(mmap(nullptr, sizeof(CounterData),
        PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0));
    
    if (counter_data_ == MAP_FAILED) {
        close(shm_fd_);
        if (created_new) {
            shm_unlink(shm_name.c_str());
        }
        throw std::system_error(errno, std::system_category(),
            "mmap failed");
    }
#endif
    
    if (created_new) {
        owns_cleanup_ = true;
        counter_data_->ref_count.store(1, std::memory_order_release);
        
#ifndef _WIN32
        // Initialize process-shared mutex
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
        
        if (pthread_mutex_init(&counter_data_->mutex, &attr) != 0) {
            pthread_mutexattr_destroy(&attr);
            munmap(counter_data_, sizeof(CounterData));
            close(shm_fd_);
            shm_unlink(shm_name.c_str());
            throw std::runtime_error("Failed to initialize shared mutex");
        }
        
        pthread_mutexattr_destroy(&attr);
#endif
        std::cout << "Created new shared memory counter: " << shm_name << std::endl;
    } else {
        // Attach to existing shared memory
        lock();
        auto current_count = counter_data_->ref_count.load(std::memory_order_acquire);
        if (current_count <= 0) {
            unlock();
            throw std::runtime_error("Shared memory is being destroyed, cannot attach");
        }
        
        counter_data_->ref_count.fetch_add(1, std::memory_order_acq_rel);
        unlock();
        
        std::cout << "Attached to existing shared memory counter: " << shm_name 
                  << ", new count: " << (current_count + 1) << std::endl;
    }
}

SharedMemoryRefCounter::~SharedMemoryRefCounter() {
    if (counter_data_) {
        decrement();
    }
    reset();
}

SharedMemoryRefCounter::SharedMemoryRefCounter(SharedMemoryRefCounter&& other) noexcept
    : shm_name_(std::move(other.shm_name_))
    , counter_data_(other.counter_data_)
    , cleanup_func_(std::move(other.cleanup_func_))
    , owns_cleanup_(other.owns_cleanup_)
#ifdef _WIN32
    , file_mapping_(other.file_mapping_)
    , mutex_(other.mutex_)
#else
    , shm_fd_(other.shm_fd_)
#endif
{
    other.counter_data_ = nullptr;
    other.owns_cleanup_ = false;
#ifdef _WIN32
    other.file_mapping_ = nullptr;
    other.mutex_ = nullptr;
#else
    other.shm_fd_ = -1;
#endif
}

SharedMemoryRefCounter& SharedMemoryRefCounter::operator=(SharedMemoryRefCounter&& other) noexcept {
    if (this != &other) {
        if (counter_data_) {
            decrement();
        }
        reset();
        
        shm_name_ = std::move(other.shm_name_);
        counter_data_ = other.counter_data_;
        cleanup_func_ = std::move(other.cleanup_func_);
        owns_cleanup_ = other.owns_cleanup_;
#ifdef _WIN32
        file_mapping_ = other.file_mapping_;
        mutex_ = other.mutex_;
        other.file_mapping_ = nullptr;
        other.mutex_ = nullptr;
#else
        shm_fd_ = other.shm_fd_;
        other.shm_fd_ = -1;
#endif
        other.counter_data_ = nullptr;
        other.owns_cleanup_ = false;
    }
    return *this;
}

int32_t SharedMemoryRefCounter::get_count() const {
    if (!counter_data_) return 0;
    return counter_data_->ref_count.load(std::memory_order_acquire);
}

int32_t SharedMemoryRefCounter::increment() {
    if (!counter_data_) {
        throw std::runtime_error("Counter not initialized");
    }
    
    lock();
    auto current_count = counter_data_->ref_count.load(std::memory_order_acquire);
    if (current_count <= 0) {
        unlock();
        throw std::runtime_error("Shared memory is being destroyed");
    }
    
    auto new_count = counter_data_->ref_count.fetch_add(1, std::memory_order_acq_rel) + 1;
    unlock();
    
    std::cout << "Incremented reference count for " << shm_name_ 
              << " to " << new_count << std::endl;
    return new_count;
}

int32_t SharedMemoryRefCounter::decrement() {
    if (!counter_data_) {
        return 0;
    }
    
    lock();
    auto remaining_count = counter_data_->ref_count.fetch_sub(1, std::memory_order_acq_rel) - 1;
    
    if (remaining_count == 0) {
        std::cout << "Last reference to " << shm_name_ << " removed" << std::endl;
        unlock();
        cleanup();
        return 0;
    } else {
        unlock();
        std::cout << "Decremented reference count for " << shm_name_ 
                  << " to " << remaining_count << std::endl;
        return remaining_count;
    }
}

void SharedMemoryRefCounter::lock() {
#ifdef _WIN32
    if (mutex_) {
        WaitForSingleObject(mutex_, INFINITE);
    }
#else
    if (counter_data_) {
        pthread_mutex_lock(&counter_data_->mutex);
    }
#endif
}

void SharedMemoryRefCounter::unlock() {
#ifdef _WIN32
    if (mutex_) {
        ReleaseMutex(mutex_);
    }
#else
    if (counter_data_) {
        pthread_mutex_unlock(&counter_data_->mutex);
    }
#endif
}

void SharedMemoryRefCounter::cleanup() {
    if (cleanup_func_) {
        try {
            cleanup_func_();
        } catch (const std::exception& e) {
            std::cerr << "Error in cleanup function for " << shm_name_ 
                      << ": " << e.what() << std::endl;
        }
    }
    
#ifdef _WIN32
    if (counter_data_) {
        UnmapViewOfFile(counter_data_);
    }
    if (file_mapping_) {
        CloseHandle(file_mapping_);
    }
    if (mutex_) {
        CloseHandle(mutex_);
    }
#else
    if (counter_data_ && counter_data_ != MAP_FAILED) {
        pthread_mutex_destroy(&counter_data_->mutex);
        munmap(counter_data_, sizeof(CounterData));
        shm_unlink(shm_name_.c_str());
    }
    if (shm_fd_ != -1) {
        close(shm_fd_);
    }
#endif
    
    std::cout << "Cleaned up shared memory: " << shm_name_ << std::endl;
}

void SharedMemoryRefCounter::reset() {
#ifdef _WIN32
    if (counter_data_) {
        UnmapViewOfFile(counter_data_);
        counter_data_ = nullptr;
    }
    if (file_mapping_) {
        CloseHandle(file_mapping_);
        file_mapping_ = nullptr;
    }
    if (mutex_) {
        CloseHandle(mutex_);
        mutex_ = nullptr;
    }
#else
    if (counter_data_ && counter_data_ != MAP_FAILED) {
        munmap(counter_data_, sizeof(CounterData));
        counter_data_ = nullptr;
    }
    if (shm_fd_ != -1) {
        close(shm_fd_);
        shm_fd_ = -1;
    }
#endif
}

} // namespace nprpc::impl
