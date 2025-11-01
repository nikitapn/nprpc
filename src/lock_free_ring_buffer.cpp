#include <nprpc/impl/lock_free_ring_buffer.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/common.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <iostream>

namespace nprpc::impl {

size_t LockFreeRingBuffer::calculate_shm_size(uint32_t capacity, uint32_t slot_size) {
    // managed_shared_memory needs space for:
    // - Internal bookkeeping (~2KB)
    // - Header object with name
    // - Data region (capacity * slot_size)
    // - Alignment padding (64 bytes per allocation)
    size_t data_size = static_cast<size_t>(capacity) * static_cast<size_t>(slot_size);
    size_t overhead = 8192;  // 8KB for managed_shared_memory overhead + bookkeeping
    return overhead + data_size;
}

std::unique_ptr<LockFreeRingBuffer> LockFreeRingBuffer::create(
    const std::string& name,
    uint32_t capacity,
    uint32_t slot_size) {
    
    try {
        // Remove any existing shared memory with this name
        boost::interprocess::shared_memory_object::remove(name.c_str());
        
        // Calculate total size needed
        size_t total_size = calculate_shm_size(capacity, slot_size);
        
        // Create managed shared memory
        std::cout << "Creating ring buffer '" << name << "'\n";
        boost::interprocess::managed_shared_memory shm(
            boost::interprocess::create_only,
            name.c_str(),
            total_size);
        
        // Construct header in shared memory
        auto* header = shm.construct<RingBufferHeader>("header")(
            capacity, slot_size, MAX_MESSAGE_SIZE);
        
        if (!header) {
            throw std::runtime_error("Failed to construct RingBufferHeader");
        }
        
        // Allocate data region (array of slots)
        // Use construct to create a named array instead of allocate_aligned
        size_t data_size = static_cast<size_t>(capacity) * static_cast<size_t>(slot_size);
        
        auto* data_region = shm.construct<uint8_t>("data")[data_size]();
        
        if (!data_region) {
            throw std::runtime_error("Failed to allocate data region");
        }
        
        if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
            std::cout << "Created ring buffer '" << name << "': " << capacity 
                     << " slots x " << slot_size << " bytes = " << total_size << " total" << std::endl;
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
            std::cout << "Opened ring buffer '" << name << "': " << header->capacity 
                     << " slots x " << header->slot_size << " bytes" << std::endl;
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

uint8_t* LockFreeRingBuffer::get_slot(uint32_t idx) {
    return data_region_ + (idx * header_->slot_size);
}

bool LockFreeRingBuffer::try_write(const void* data, size_t size) {
    // Validate message size
    if (size > header_->max_message_size) {
        std::cerr << "Message size " << size << " exceeds maximum " 
                 << header_->max_message_size << std::endl;
        return false;
    }
    
    if (size > header_->slot_size - sizeof(uint32_t)) {
        std::cerr << "Message size " << size << " exceeds slot capacity " 
                 << (header_->slot_size - sizeof(uint32_t)) << std::endl;
        return false;
    }
    
    // Load indices with acquire semantics
    uint32_t write_idx = header_->write_idx.load(std::memory_order_acquire);
    uint32_t read_idx = header_->read_idx.load(std::memory_order_acquire);
    
    // Check if buffer is full
    uint32_t next_write = (write_idx + 1) % header_->capacity;
    if (next_write == read_idx) {
        return false; // Buffer full
    }
    
    // Write to slot
    uint8_t* slot = get_slot(write_idx);
    
    // Write size header
    *reinterpret_cast<uint32_t*>(slot) = static_cast<uint32_t>(size);
    
    // Write data
    std::memcpy(slot + sizeof(uint32_t), data, size);
    
    // Update write index with release semantics (ensures writes are visible)
    header_->write_idx.store(next_write, std::memory_order_release);
    
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
    uint32_t read_idx = header_->read_idx.load(std::memory_order_acquire);
    uint32_t write_idx = header_->write_idx.load(std::memory_order_acquire);
    
    // Check if buffer is empty
    if (read_idx == write_idx) {
        return 0; // Empty
    }
    
    // Read from slot
    uint8_t* slot = get_slot(read_idx);
    
    // Read size header
    uint32_t message_size = *reinterpret_cast<uint32_t*>(slot);
    
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
    
    // Copy data
    std::memcpy(buffer, slot + sizeof(uint32_t), message_size);
    
    // Update read index with release semantics
    uint32_t next_read = (read_idx + 1) % header_->capacity;
    header_->read_idx.store(next_read, std::memory_order_release);
    
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

uint32_t LockFreeRingBuffer::available_slots() const {
    uint32_t read_idx = header_->read_idx.load(std::memory_order_acquire);
    uint32_t write_idx = header_->write_idx.load(std::memory_order_acquire);
    
    if (write_idx >= read_idx) {
        return header_->capacity - (write_idx - read_idx) - 1;
    } else {
        return read_idx - write_idx - 1;
    }
}

bool LockFreeRingBuffer::is_empty() const {
    uint32_t read_idx = header_->read_idx.load(std::memory_order_acquire);
    uint32_t write_idx = header_->write_idx.load(std::memory_order_acquire);
    return read_idx == write_idx;
}

bool LockFreeRingBuffer::is_full() const {
    uint32_t read_idx = header_->read_idx.load(std::memory_order_acquire);
    uint32_t write_idx = header_->write_idx.load(std::memory_order_acquire);
    uint32_t next_write = (write_idx + 1) % header_->capacity;
    return next_write == read_idx;
}

} // namespace nprpc::impl
