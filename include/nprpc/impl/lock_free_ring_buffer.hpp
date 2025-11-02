#ifndef NPRPC_IMPL_LOCK_FREE_RING_BUFFER_HPP_
#define NPRPC_IMPL_LOCK_FREE_RING_BUFFER_HPP_

#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/interprocess_condition.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace nprpc::impl {

// Lock-free ring buffer for single producer, single consumer (SPSC)
// Uses memory-mapped shared memory for true zero-copy IPC
//
// Memory Layout:
// +------------------+
// | RingBufferHeader | (metadata: read_idx, write_idx, buffer_size, etc.)
// +------------------+
// | Continuous Buffer| (circular byte array for variable-sized messages)
// | [uint32_t size]  | <- Message 1
// | [data bytes]     |
// | [uint32_t size]  | <- Message 2
// | [data bytes]     |
// | ...              |
// +------------------+
//
// Message format: [uint32_t size][data bytes]
// - Variable-sized messages (no wasted space)
// - Wraparound handled automatically
// - Byte-level indices (not slot-based)
//
// Synchronization:
// - read_idx and write_idx are atomics with byte offsets (lock-free for SPSC)
// - For blocking reads: condition variable + mutex
// - Memory barriers handled by std::atomic

struct alignas(64) RingBufferHeader {
    // Atomic byte-level indices for lock-free operations
    std::atomic<size_t> write_idx{0};  // Next byte position to write
    std::atomic<size_t> read_idx{0};   // Next byte position to read
    
    // Fixed at creation
    size_t buffer_size;         // Total buffer size in bytes
    uint32_t max_message_size;  // Maximum message size
    
    // For blocking reads (optional - can still be lock-free with timed_wait)
    boost::interprocess::interprocess_mutex mutex;
    boost::interprocess::interprocess_condition data_available;
    
    RingBufferHeader(size_t buf_size, uint32_t max_msg_sz)
        : buffer_size(buf_size)
        , max_message_size(max_msg_sz) {
    }
};

class LockFreeRingBuffer {
public:
    // Configuration for continuous circular buffer (variable-sized messages)
    static constexpr size_t DEFAULT_BUFFER_SIZE = 16 * 1024 * 1024; // 16MB total (reduced from 128MB)
    static constexpr uint32_t MAX_MESSAGE_SIZE = 32 * 1024 * 1024;  // 32MB max message
    
    // Create new ring buffer in shared memory
    static std::unique_ptr<LockFreeRingBuffer> create(
        const std::string& name,
        size_t buffer_size = DEFAULT_BUFFER_SIZE);
    
    // Open existing ring buffer
    static std::unique_ptr<LockFreeRingBuffer> open(const std::string& name);
    
    // Remove shared memory region (call when destroying channel)
    static void remove(const std::string& name);
    
    ~LockFreeRingBuffer();
    
    // Non-blocking write
    // Returns true if message was written, false if buffer full
    bool try_write(const void* data, size_t size);
    
    // Non-blocking read
    // Returns number of bytes read (0 if empty)
    size_t try_read(void* buffer, size_t buffer_size);
    
    // Blocking read with timeout
    // Returns number of bytes read (0 on timeout)
    size_t read_with_timeout(void* buffer, size_t buffer_size, 
                             std::chrono::milliseconds timeout);
    
    // Statistics
    size_t buffer_size() const { return header_->buffer_size; }
    size_t available_bytes() const;
    bool is_empty() const;
    bool is_full(size_t message_size) const;
    
private:
    LockFreeRingBuffer(boost::interprocess::managed_shared_memory&& shm,
                       RingBufferHeader* header,
                       uint8_t* data_region,
                       bool is_creator);
    
    // Calculate total shared memory size needed
    static size_t calculate_shm_size(size_t buffer_size);
    
    // Helper to calculate used bytes
    size_t used_bytes() const;
    
    boost::interprocess::managed_shared_memory shm_;
    RingBufferHeader* header_;
    uint8_t* data_region_;  // Points to start of slot array
    bool is_creator_;       // Should we remove shm on destruction?
};

// Helper: Generate unique names for shared memory regions
inline std::string make_shm_name(const std::string& channel_id, const std::string& direction) {
    return "/nprpc_" + channel_id + "_" + direction;
}

} // namespace nprpc::impl

#endif // NPRPC_IMPL_LOCK_FREE_RING_BUFFER_HPP_
