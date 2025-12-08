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
    alignas(64) std::atomic<size_t> write_idx{0};  // Next byte position to write
    alignas(64) std::atomic<size_t> read_idx{0};   // Next byte position to read
    
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
    static constexpr size_t DEFAULT_BUFFER_SIZE = 16 * 1024 * 1024; // 16MB total
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
    
    //--------------------------------------------------------------------------
    // Zero-copy API for direct buffer access
    //--------------------------------------------------------------------------
    
    struct WriteReservation {
        uint8_t* data;      // Pointer to write data (after size header)
        size_t max_size;    // Maximum bytes that can be written
        size_t write_idx;   // Internal: position where size header was written
        bool valid;         // true if reservation succeeded
        
        explicit operator bool() const { return valid; }
    };
    
    struct ReadView {
        const uint8_t* data;  // Pointer to message data (after size header)
        size_t size;          // Message size in bytes
        size_t read_idx;      // Internal: next read position after this message
        bool valid;           // true if read succeeded
        
        explicit operator bool() const { return valid; }
    };
    
    // Reserve space for writing a message (zero-copy write)
    // Call commit_write() after writing to complete the operation
    // @param min_size Minimum size you need (will fail if not available)
    // @return WriteReservation with pointer and max_size (full available space), or invalid if no space
    WriteReservation try_reserve_write(size_t min_size);
    
    // Commit a reserved write with the actual size written
    // Must be called after try_reserve_write() with the actual bytes written
    void commit_write(const WriteReservation& reservation, size_t actual_size);
    
    // Get a read view into the ring buffer (zero-copy read)
    // Call commit_read() after processing to advance read pointer
    // @return ReadView with data pointer, or invalid if empty
    ReadView try_read_view();
    
    // Commit a read, advancing the read pointer
    void commit_read(const ReadView& view);
    
    // Statistics
    size_t buffer_size() const { return header_->buffer_size; }
    size_t available_bytes() const;
    bool is_empty() const;
    bool is_full(size_t message_size) const;
    
    // Access to header for synchronization (used by SharedMemoryChannel)
    RingBufferHeader* header() { return header_; }
    
private:
    LockFreeRingBuffer(const std::string& name,
                       boost::interprocess::managed_shared_memory&& shm,
                       RingBufferHeader* header,
                       uint8_t* data_region,
                       void* mirror_base,
                       size_t ring_window,
                       bool is_creator);
    
    // Calculate total shared memory size needed
    static size_t calculate_shm_size(size_t buffer_size);
    
    // Helper to calculate used bytes
    size_t used_bytes() const;
    
    std::string name_;
    boost::interprocess::managed_shared_memory shm_;
    RingBufferHeader* header_;
    uint8_t* data_region_;  // Points to start of mirrored data region
    void* mirror_base_;     // Base address for munmap
    size_t ring_window_;    // Size of each mapped window (page-aligned)
    bool is_creator_;       // Should we remove shm on destruction?
};

// Helper: Generate unique names for shared memory regions
inline std::string make_shm_name(const std::string& channel_id, const std::string& direction) {
    return "/nprpc_" + channel_id + "_" + direction;
}

} // namespace nprpc::impl

#endif // NPRPC_IMPL_LOCK_FREE_RING_BUFFER_HPP_
