#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <functional>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#endif

namespace nprpc::impl {

/**
 * @brief Reliable reference counter for shared memory objects
 * 
 * This class provides atomic reference counting for shared memory objects
 * with proper cleanup when the last reference is removed. It handles
 * race conditions that can occur when multiple processes are attaching
 * and detaching from shared memory simultaneously.
 */
class SharedMemoryRefCounter {
public:
    /**
     * @brief Cleanup function type called when last reference is removed
     */
    using CleanupFunction = std::function<void()>;

private:
    struct CounterData {
        std::atomic<int32_t> ref_count{0};
#ifndef _WIN32
        pthread_mutex_t mutex;
#endif
    };

    std::string shm_name_;
    CounterData* counter_data_ = nullptr;
    CleanupFunction cleanup_func_;
    bool owns_cleanup_ = false;

#ifdef _WIN32
    HANDLE file_mapping_ = nullptr;
    HANDLE mutex_ = nullptr;
#else
    int shm_fd_ = -1;
#endif

public:
    /**
     * @brief Constructor
     * @param shm_name Name of the shared memory object
     * @param cleanup_func Function to call when last reference is removed
     */
    explicit SharedMemoryRefCounter(const std::string& shm_name, 
                                  CleanupFunction cleanup_func = nullptr);

    /**
     * @brief Destructor - automatically decrements reference count
     */
    ~SharedMemoryRefCounter();

    // Non-copyable, movable
    SharedMemoryRefCounter(const SharedMemoryRefCounter&) = delete;
    SharedMemoryRefCounter& operator=(const SharedMemoryRefCounter&) = delete;
    SharedMemoryRefCounter(SharedMemoryRefCounter&& other) noexcept;
    SharedMemoryRefCounter& operator=(SharedMemoryRefCounter&& other) noexcept;

    /**
     * @brief Get current reference count
     * @return Current number of references
     */
    int32_t get_count() const;

    /**
     * @brief Manually increment reference count
     * @return New reference count after increment
     * @throws std::runtime_error if shared memory is being destroyed
     */
    int32_t increment();

    /**
     * @brief Manually decrement reference count
     * @return New reference count after decrement
     * @note If count reaches 0, cleanup function is called
     */
    int32_t decrement();

    /**
     * @brief Check if this instance owns the cleanup responsibility
     * @return true if this instance will call cleanup when count reaches 0
     */
    bool owns_cleanup() const { return owns_cleanup_; }

private:
    void lock();
    void unlock();
    void cleanup();
    void reset();
};

/**
 * @brief RAII wrapper for shared memory reference counting
 * 
 * Automatically manages reference counting for shared memory objects.
 * When the last SharedMemoryRef is destroyed, the cleanup function is called.
 */
template<typename T>
class SharedMemoryRef {
private:
    std::unique_ptr<SharedMemoryRefCounter> counter_;
    T* ptr_ = nullptr;

public:
    /**
     * @brief Constructor
     * @param shm_name Name of the shared memory object
     * @param ptr Pointer to the shared memory object
     * @param cleanup_func Function to call when last reference is removed
     */
    SharedMemoryRef(const std::string& shm_name, T* ptr, 
                   SharedMemoryRefCounter::CleanupFunction cleanup_func = nullptr)
        : counter_(std::make_unique<SharedMemoryRefCounter>(shm_name, cleanup_func))
        , ptr_(ptr) {}

    /**
     * @brief Copy constructor - increments reference count
     */
    SharedMemoryRef(const SharedMemoryRef& other)
        : counter_(std::make_unique<SharedMemoryRefCounter>(
            other.counter_->shm_name_, other.counter_->cleanup_func_))
        , ptr_(other.ptr_) {
        counter_->increment();
    }

    /**
     * @brief Move constructor
     */
    SharedMemoryRef(SharedMemoryRef&& other) noexcept
        : counter_(std::move(other.counter_))
        , ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }

    /**
     * @brief Assignment operator
     */
    SharedMemoryRef& operator=(const SharedMemoryRef& other) {
        if (this != &other) {
            counter_.reset();
            counter_ = std::make_unique<SharedMemoryRefCounter>(
                other.counter_->shm_name_, other.counter_->cleanup_func_);
            ptr_ = other.ptr_;
            counter_->increment();
        }
        return *this;
    }

    /**
     * @brief Move assignment operator
     */
    SharedMemoryRef& operator=(SharedMemoryRef&& other) noexcept {
        if (this != &other) {
            counter_ = std::move(other.counter_);
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    /**
     * @brief Destructor - automatically decrements reference count
     */
    ~SharedMemoryRef() = default;

    /**
     * @brief Get pointer to shared memory object
     */
    T* get() const { return ptr_; }

    /**
     * @brief Dereference operator
     */
    T& operator*() const { return *ptr_; }

    /**
     * @brief Arrow operator
     */
    T* operator->() const { return ptr_; }

    /**
     * @brief Boolean conversion
     */
    explicit operator bool() const { return ptr_ != nullptr; }

    /**
     * @brief Get current reference count
     */
    int32_t ref_count() const { 
        return counter_ ? counter_->get_count() : 0; 
    }
};

} // namespace nprpc::impl
