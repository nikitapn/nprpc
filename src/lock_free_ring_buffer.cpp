#include <nprpc/impl/lock_free_ring_buffer.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/common.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <iostream>

namespace nprpc::impl {

size_t LockFreeRingBuffer::calculate_shm_size(size_t buffer_size) {
    // managed_shared_memory needs space for:
    // - Internal bookkeeping (~2KB)
    // - Header object with name
    // - Data region (continuous buffer)
    // - Alignment padding (64 bytes per allocation)
    size_t overhead = 8192;  // 8KB for managed_shared_memory overhead + bookkeeping
    return overhead + buffer_size;
}

std::unique_ptr<LockFreeRingBuffer> LockFreeRingBuffer::create(
    const std::string& name,
    size_t buffer_size) {
    
    try {
        // Remove any existing shared memory with this name
        boost::interprocess::shared_memory_object::remove(name.c_str());
        
        // Calculate total size needed
        size_t total_size = calculate_shm_size(buffer_size);
        
        // Create managed shared memory
        std::cout << "Creating ring buffer '" << name << "'\n";
        boost::interprocess::managed_shared_memory shm(
            boost::interprocess::create_only,
            name.c_str(),
            total_size);
        
        // Construct header in shared memory
        auto* header = shm.construct<RingBufferHeader>("header")(
            buffer_size, MAX_MESSAGE_SIZE);
        
        if (!header) {
            throw std::runtime_error("Failed to construct RingBufferHeader");
        }
        
        // Allocate data region (continuous circular buffer)
        auto* data_region = shm.construct<uint8_t>("data")[buffer_size]();
        
        if (!data_region) {
            throw std::runtime_error("Failed to allocate data region");
        }
        
        if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
            std::cout << "Created ring buffer '" << name << "': " 
                     << buffer_size << " bytes (continuous)" << std::endl;
        }
        
        return std::unique_ptr<LockFreeRingBuffer>(
            new LockFreeRingBuffer(std::move(shm), header, data_region, true));
        
    } catch (const boost::interprocess::interprocess_exception& e) {
        std::cerr << "Failed to create ring buffer '" << name << "': " << e.what() << std::endl;
        throw;
    }
}

std::unique_ptr<LockFreeRingBuffer> LockFreeRingBuffer::open(const std::string& name) {
    try {
        // Open existing shared memory
        std::cout << "Opening ring buffer '" << name << "'\n";
        boost::interprocess::managed_shared_memory shm(
            boost::interprocess::open_only,
            name.c_str());
        
        // Find header
        auto result = shm.find<RingBufferHeader>("header");
        if (result.first == nullptr) {
            throw std::runtime_error("Header not found in shared memory");
        }
        
        auto* header = result.first;
        
        // Find data region by name
        auto data_result = shm.find<uint8_t>("data");
        if (data_result.first == nullptr) {
            throw std::runtime_error("Data region not found in shared memory");
        }
        
        auto* data_region = data_result.first;
        
        if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
            std::cout << "Opened ring buffer '" << name << "': " 
                     << header->buffer_size << " bytes (continuous)" << std::endl;
        }
        
        return std::unique_ptr<LockFreeRingBuffer>(
            new LockFreeRingBuffer(std::move(shm), header, data_region, false));
        
    } catch (const boost::interprocess::interprocess_exception& e) {
        std::cerr << "Failed to open ring buffer '" << name << "': " << e.what() << std::endl;
        throw;
    }
}

void LockFreeRingBuffer::remove(const std::string& name) {
    boost::interprocess::shared_memory_object::remove(name.c_str());
    if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
        std::cout << "Removed ring buffer '" << name << "'" << std::endl;
    }
}

LockFreeRingBuffer::LockFreeRingBuffer(
    boost::interprocess::managed_shared_memory&& shm,
    RingBufferHeader* header,
    uint8_t* data_region,
    bool is_creator)
    : shm_(std::move(shm))
    , header_(header)
    , data_region_(data_region)
    , is_creator_(is_creator) {
}

LockFreeRingBuffer::~LockFreeRingBuffer() {
    // Cleanup is automatic - managed_shared_memory destructor handles it
    // But we should remove the shared memory object if we created it
    if (is_creator_) {
        // Can't call remove() here because name is not stored
        // Caller must explicitly call static remove() if needed
    }
}

size_t LockFreeRingBuffer::used_bytes() const {
    size_t write_idx = header_->write_idx.load(std::memory_order_acquire);
    size_t read_idx = header_->read_idx.load(std::memory_order_acquire);
    
    if (write_idx >= read_idx) {
        return write_idx - read_idx;
    } else {
        // Wrapped around
        return header_->buffer_size - read_idx + write_idx;
    }
}

bool LockFreeRingBuffer::try_write(const void* data, size_t size) {
    // Validate message size
    if (size > header_->max_message_size) {
        std::cerr << "Message size " << size << " exceeds maximum " 
                 << header_->max_message_size << std::endl;
        return false;
    }
    
    // Total bytes needed: size header (4 bytes) + data
    size_t total_bytes = sizeof(uint32_t) + size;
    
    // Check if we have enough space
    // Need to keep at least 1 byte free to distinguish full from empty
    if (total_bytes + 1 > available_bytes()) {
        return false; // Buffer full
    }
    
    // Load current write index
    size_t write_idx = header_->write_idx.load(std::memory_order_acquire);
    
    // Write size header (may wrap around)
    for (size_t i = 0; i < sizeof(uint32_t); ++i) {
        data_region_[(write_idx + i) % header_->buffer_size] = 
            reinterpret_cast<const uint8_t*>(&size)[i];
    }
    write_idx = (write_idx + sizeof(uint32_t)) % header_->buffer_size;
    
    // Write data (may wrap around)
    const uint8_t* src = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        data_region_[(write_idx + i) % header_->buffer_size] = src[i];
    }
    
    // Update write index with release semantics
    size_t new_write_idx = (write_idx + size) % header_->buffer_size;
    header_->write_idx.store(new_write_idx, std::memory_order_release);
    
    // Notify waiting readers
    {
        boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex> 
            lock(header_->mutex);
        header_->data_available.notify_one();
    }
    
    return true;
}

size_t LockFreeRingBuffer::try_read(void* buffer, size_t buffer_size) {
    // Load indices with acquire semantics
    size_t read_idx = header_->read_idx.load(std::memory_order_acquire);
    size_t write_idx = header_->write_idx.load(std::memory_order_acquire);
    
    // Check if buffer is empty
    if (read_idx == write_idx) {
        return 0; // Empty
    }
    
    // Read size header (may wrap around)
    uint32_t message_size = 0;
    for (size_t i = 0; i < sizeof(uint32_t); ++i) {
        reinterpret_cast<uint8_t*>(&message_size)[i] = 
            data_region_[(read_idx + i) % header_->buffer_size];
    }
    read_idx = (read_idx + sizeof(uint32_t)) % header_->buffer_size;
    
    // Validate size
    if (message_size > header_->max_message_size) {
        std::cerr << "Corrupt message size: " << message_size << std::endl;
        return 0;
    }
    
    if (message_size > buffer_size) {
        std::cerr << "Buffer too small: message=" << message_size 
                 << ", buffer=" << buffer_size << std::endl;
        return 0;
    }
    
    // Copy data (may wrap around)
    uint8_t* dest = static_cast<uint8_t*>(buffer);
    for (size_t i = 0; i < message_size; ++i) {
        dest[i] = data_region_[(read_idx + i) % header_->buffer_size];
    }
    
    // Update read index with release semantics
    size_t new_read_idx = (read_idx + message_size) % header_->buffer_size;
    header_->read_idx.store(new_read_idx, std::memory_order_release);
    
    return message_size;
}

size_t LockFreeRingBuffer::read_with_timeout(
    void* buffer, 
    size_t buffer_size,
    std::chrono::milliseconds timeout) {
    
    // First try non-blocking read
    size_t bytes = try_read(buffer, buffer_size);
    if (bytes > 0) {
        return bytes;
    }
    
    // Wait for data with timeout
    boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex> 
        lock(header_->mutex);
    
    // Calculate absolute deadline in boost posix_time
    auto deadline = boost::posix_time::microsec_clock::universal_time() + 
                    boost::posix_time::milliseconds(timeout.count());
    
    while (is_empty()) {
        auto now = boost::posix_time::microsec_clock::universal_time();
        if (now >= deadline) {
            return 0; // Timeout
        }
        
        // Wait with absolute time
        if (!header_->data_available.timed_wait(lock, deadline)) {
            return 0; // Timeout
        }
    }
    
    // Release lock and try read again
    lock.unlock();
    return try_read(buffer, buffer_size);
}

size_t LockFreeRingBuffer::available_bytes() const {
    size_t used = used_bytes();
    // Keep 1 byte reserved to distinguish full from empty
    return header_->buffer_size - used - 1;
}

bool LockFreeRingBuffer::is_empty() const {
    size_t read_idx = header_->read_idx.load(std::memory_order_acquire);
    size_t write_idx = header_->write_idx.load(std::memory_order_acquire);
    return read_idx == write_idx;
}

bool LockFreeRingBuffer::is_full(size_t message_size) const {
    size_t total_bytes = sizeof(uint32_t) + message_size;
    return total_bytes + 1 > available_bytes();
}

} // namespace nprpc::impl
