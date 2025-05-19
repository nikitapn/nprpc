#pragma once

#include <cstdint>
#include <array>
#include <atomic>
#include <cstring> // for memcpy

namespace nprpc::impl {

// Ring buffer header stored in shared memory
struct SharedRingBufferHeader {
    static constexpr uint32_t BUFFER_SIZE = 1024 * 1024; // 1MB
    static constexpr uint32_t MAGIC = 0x4E505243; // "NPRPC"
    
    uint32_t magic;
    std::atomic<uint32_t> write_pos;
    std::atomic<uint32_t> read_pos;
    std::atomic<uint32_t> connection_count;
    std::atomic<bool> server_ready;
    char _padding[44]; // Pad to 64 bytes for cache line alignment
};

// Full ring buffer structure
struct SharedRingBuffer {
    SharedRingBufferHeader header;
    char buffer[SharedRingBufferHeader::BUFFER_SIZE];

    bool try_write(const void* data, uint32_t size) {
        if (size > SharedRingBufferHeader::BUFFER_SIZE - 1) return false;

        uint32_t write = header.write_pos.load(std::memory_order_relaxed);
        uint32_t read = header.read_pos.load(std::memory_order_acquire);
        
        uint32_t available = (read > write) ? 
            read - write - 1 : 
            SharedRingBufferHeader::BUFFER_SIZE - (write - read) - 1;

        if (available < size + sizeof(uint32_t)) return false;

        // Write size prefix
        *reinterpret_cast<uint32_t*>(&buffer[write]) = size;
        write = (write + sizeof(uint32_t)) % SharedRingBufferHeader::BUFFER_SIZE;

        // Write data in two parts if wrapping around buffer end
        uint32_t first_chunk = std::min(size, 
            SharedRingBufferHeader::BUFFER_SIZE - write);
        memcpy(&buffer[write], data, first_chunk);
        
        if (first_chunk < size) {
            memcpy(&buffer[0], 
                static_cast<const char*>(data) + first_chunk, 
                size - first_chunk);
        }

        write = (write + size) % SharedRingBufferHeader::BUFFER_SIZE;
        header.write_pos.store(write, std::memory_order_release);
        return true;
    }

    bool try_read(void* data, uint32_t& size) {
        uint32_t read = header.read_pos.load(std::memory_order_relaxed);
        uint32_t write = header.write_pos.load(std::memory_order_acquire);

        if (read == write) return false;

        // Read size prefix
        uint32_t msg_size = *reinterpret_cast<uint32_t*>(&buffer[read]);
        read = (read + sizeof(uint32_t)) % SharedRingBufferHeader::BUFFER_SIZE;

        // Read data in two parts if wrapping around buffer end
        uint32_t first_chunk = std::min(msg_size, 
            SharedRingBufferHeader::BUFFER_SIZE - read);
        memcpy(data, &buffer[read], first_chunk);
        
        if (first_chunk < msg_size) {
            memcpy(static_cast<char*>(data) + first_chunk, 
                &buffer[0], 
                msg_size - first_chunk);
        }

        read = (read + msg_size) % SharedRingBufferHeader::BUFFER_SIZE;
        header.read_pos.store(read, std::memory_order_release);
        size = msg_size;
        return true;
    }
};

} // namespace nprpc::impl