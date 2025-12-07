#include <nprpc/impl/lock_free_ring_buffer.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/common.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <iostream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace nprpc::impl {

// Helper to get page size
static size_t get_page_size() {
    static size_t page_size = static_cast<size_t>(sysconf(_SC_PAGE_SIZE));
    return page_size;
}

size_t LockFreeRingBuffer::calculate_shm_size(size_t buffer_size) {
    // Memory layout must ensure data region starts at page-aligned offset
    // to avoid double-mapping the header (which contains pthread mutex).
    //
    // Layout:
    // [0 ... ~2KB)        - Boost managed_shared_memory bookkeeping
    // [~2KB ... ~2.5KB)   - Header object with name string
    // [~2.5KB ... 4KB)    - Padding to reach page boundary
    // [4KB ... 4KB+size)  - Data region (page-aligned start)
    //
    // We reserve one full page for overhead to guarantee data starts on page boundary
    size_t page_size = get_page_size();
    size_t overhead = page_size;  // One page for header + bookkeeping
    return overhead + buffer_size;
}

std::unique_ptr<LockFreeRingBuffer> LockFreeRingBuffer::create(
    const std::string& name,
    size_t buffer_size) {
    try {
        size_t page_size = get_page_size();

        // Calculate total size: one page for header/bookkeeping + buffer
        // Data will start exactly at page_size offset
        size_t total_size = page_size + buffer_size;

        // 1) Create managed shared memory (Boost)
        boost::interprocess::managed_shared_memory shm(
            boost::interprocess::create_only,
            name.c_str(),
            total_size);

        // 2) Construct header (Boost will place it within the first page)
        auto* header = shm.construct<RingBufferHeader>("header")(
            buffer_size, MAX_MESSAGE_SIZE);
        if (!header) throw std::runtime_error("Failed to construct RingBufferHeader");

        // 3) Verify header fits within first page
        uint8_t* shm_base = static_cast<uint8_t*>(shm.get_address());
        size_t header_offset = reinterpret_cast<uint8_t*>(header) - shm_base;
        size_t header_end = header_offset + sizeof(RingBufferHeader);

        if (header_end > page_size) {
            throw std::runtime_error("Header doesn't fit in first page - increase page reservation");
        }

        // 4) Data region starts exactly at page_size offset (page-aligned)
        // We don't use Boost's construct because we need precise control over placement
        uint8_t* data_region = shm_base + page_size;

        // Store data offset in header for open() to find it
        // (We're repurposing unused space or adding a field if needed)
        // Actually, we can calculate it: data always starts at page_size offset

        // 5) Ring window is exactly buffer_size (rounded up to page)
        size_t ring_window = (buffer_size + page_size - 1) & ~(page_size - 1);

        // Reserve 2x ring window for mirrored mapping
        void* reserved = mmap(nullptr, 2 * ring_window, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (reserved == MAP_FAILED) {
            throw std::runtime_error("Failed to reserve virtual address space for mirrored mapping");
        }

        // 6) Open the POSIX shm object to get an fd
        std::string posix_name = name;
        if (posix_name.empty() || posix_name[0] != '/') posix_name.insert(posix_name.begin(), '/');

        int fd = shm_open(posix_name.c_str(), O_RDWR | O_CLOEXEC, 0600);
        if (fd == -1) {
            munmap(reserved, 2 * ring_window);
            throw std::runtime_error(std::string("Failed to open shared memory: ") + strerror(errno));
        }

        // 7) Map only the DATA region (starting from page_size offset), not the header!
        void* first_map = mmap(reserved, ring_window, PROT_READ | PROT_WRITE, 
                               MAP_SHARED | MAP_FIXED, fd, page_size);
        if (first_map == MAP_FAILED || first_map != reserved) {
            close(fd);
            munmap(reserved, 2 * ring_window);
            throw std::runtime_error(std::string("Failed to create first memory mapping: ") + strerror(errno));
        }

        void* second_map_addr = static_cast<uint8_t*>(reserved) + ring_window;
        void* second_map = mmap(second_map_addr, ring_window, PROT_READ | PROT_WRITE, 
                                MAP_SHARED | MAP_FIXED, fd, page_size);
        if (second_map == MAP_FAILED || second_map != second_map_addr) {
            munmap(first_map, ring_window);
            close(fd);
            munmap(reserved, 2 * ring_window);
            throw std::runtime_error(std::string("Failed to create mirrored memory mapping: ") + strerror(errno));
        }

        close(fd);

        // 8) Mirrored data region pointer - points to start of reserved area
        uint8_t* mirrored_data_region = static_cast<uint8_t*>(reserved);

        if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
            std::cout << "Created ring buffer '" << name << "': "
                      << buffer_size << " bytes with mirrored mapping at " 
                      << static_cast<void*>(mirrored_data_region)
                      << " (ring_window=" << ring_window << ")\n";
        }

        return std::unique_ptr<LockFreeRingBuffer>(
            new LockFreeRingBuffer(name, std::move(shm), header, mirrored_data_region, reserved, ring_window, true)
        );

    } catch (const boost::interprocess::interprocess_exception& e) {
        std::cerr << "Failed to create ring buffer '" << name << "': " << e.what() << std::endl;
        throw;
    }
}

std::unique_ptr<LockFreeRingBuffer> LockFreeRingBuffer::open(const std::string& name) {
    try {

        boost::interprocess::managed_shared_memory shm(
            boost::interprocess::open_only,
            name.c_str());

        // Find header
        auto result = shm.find<RingBufferHeader>("header");
        if (result.first == nullptr) throw std::runtime_error("Header not found in shared memory");
        RingBufferHeader* header = result.first;

        size_t buffer_size = header->buffer_size;
        size_t page_size = get_page_size();

        // Data region starts at page_size offset (page-aligned, same as in create())
        // We don't use Boost's find for data - we know exactly where it is

        // Ring window is buffer_size rounded up to page boundary
        size_t ring_window = (buffer_size + page_size - 1) & ~(page_size - 1);

        // Reserve virtual address space for mirrored data region
        void* reserved = mmap(nullptr, 2 * ring_window, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (reserved == MAP_FAILED) throw std::runtime_error("Failed to reserve virtual address space for mirrored mapping");

        // Normalize POSIX name
        std::string posix_name = name;
        if (posix_name.empty() || posix_name[0] != '/') posix_name.insert(posix_name.begin(), '/');

        int fd = shm_open(posix_name.c_str(), O_RDWR | O_CLOEXEC, 0600);
        if (fd == -1) {
            munmap(reserved, 2 * ring_window);
            throw std::runtime_error(std::string("Failed to open shared memory: ") + strerror(errno));
        }

        // Map only the DATA region (starting from page_size offset), not the header!
        void* first_map = mmap(reserved, ring_window, PROT_READ | PROT_WRITE, 
                               MAP_SHARED | MAP_FIXED, fd, page_size);
        if (first_map == MAP_FAILED || first_map != reserved) {
            close(fd);
            munmap(reserved, 2 * ring_window);
            throw std::runtime_error(std::string("Failed to create first memory mapping: ") + strerror(errno));
        }

        void* second_map_addr = static_cast<uint8_t*>(reserved) + ring_window;
        void* second_map = mmap(second_map_addr, ring_window, PROT_READ | PROT_WRITE, 
                                MAP_SHARED | MAP_FIXED, fd, page_size);
        if (second_map == MAP_FAILED || second_map != second_map_addr) {
            munmap(first_map, ring_window);
            close(fd);
            munmap(reserved, 2 * ring_window);
            throw std::runtime_error(std::string("Failed to create mirrored memory mapping: ") + strerror(errno));
        }

        close(fd);

        // Mirrored data region pointer - points to start of reserved area
        uint8_t* mirrored_data_region = static_cast<uint8_t*>(reserved);

        // if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
            std::cout << "Opened ring buffer '" << name << "': "
                      << buffer_size << " bytes with mirrored mapping at "
                      << static_cast<void*>(mirrored_data_region) << " (ring_window=" << ring_window << ")\n";
        // }

        return std::unique_ptr<LockFreeRingBuffer>(
            new LockFreeRingBuffer(name, std::move(shm), header, mirrored_data_region, reserved, ring_window, false)
        );

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
    const std::string& name,
    boost::interprocess::managed_shared_memory&& shm,
    RingBufferHeader* header,
    uint8_t* data_region,
    void* mirror_base,
    size_t ring_window,
    bool is_creator)
    : name_(name)
    , shm_(std::move(shm))
    , header_(header)
    , data_region_(data_region)
    , mirror_base_(mirror_base)
    , ring_window_(ring_window)
    , is_creator_(is_creator) {
}

LockFreeRingBuffer::~LockFreeRingBuffer() {
    // Unmap the mirrored virtual memory (2x ring_window_)
    if (mirror_base_) {
        munmap(mirror_base_, 2 * ring_window_);
    }

    // Cleanup is automatic - managed_shared_memory destructor handles it
    // But we should remove the shared memory object if we created it
    if (is_creator_) {
        auto ok = boost::interprocess::shared_memory_object::remove(name_.c_str());
        if (!ok) {
            std::cerr << "Warning: Failed to remove shared memory for ring buffer '"
                      << name_ << "'" << std::endl;
        } else if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
            std::cout << "LockFreeRingBuffer destroyed and removed: '" 
                      << name_ << "'" << std::endl;
        }
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

    // Write size header - with mirrored mapping, no wrap-around needed!
    // Just write directly, the mirror handles the wrap
    std::memcpy(data_region_ + write_idx, &size, sizeof(uint32_t));
    write_idx = (write_idx + sizeof(uint32_t)) % header_->buffer_size;

    // Write data - with mirrored mapping, single memcpy always works!
    // Even if it crosses the buffer boundary, the mirror makes it contiguous
    const uint8_t* src = static_cast<const uint8_t*>(data);
    std::memcpy(data_region_ + write_idx, src, size);

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

    // Read size header - with mirrored mapping, no wrap-around needed!
    uint32_t message_size = 0;
    std::memcpy(&message_size, data_region_ + read_idx, sizeof(uint32_t));
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

    // Copy data - with mirrored mapping, single memcpy always works!
    uint8_t* dest = static_cast<uint8_t*>(buffer);
    std::memcpy(dest, data_region_ + read_idx, message_size);

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

//------------------------------------------------------------------------------
// Zero-copy API implementations
//------------------------------------------------------------------------------

LockFreeRingBuffer::WriteReservation LockFreeRingBuffer::try_reserve_write(size_t min_size) {
    WriteReservation result{nullptr, 0, 0, false};

    // Calculate available space for data (excluding size header overhead)
    size_t avail = available_bytes();
    if (avail <= sizeof(uint32_t) + 1) {
        return result; // Not enough space for even a minimal message
    }

    // Maximum data we can write (reserve space for size header + 1 byte to distinguish full/empty)
    size_t max_data_size = avail - sizeof(uint32_t) - 1;

    // Clamp to MAX_MESSAGE_SIZE
    max_data_size = std::min(max_data_size, static_cast<size_t>(header_->max_message_size));

    // Check if we have at least the minimum requested
    if (max_data_size < min_size) {
        return result; // Buffer too full for the minimum requested size
    }

    // Load current write index
    size_t write_idx = header_->write_idx.load(std::memory_order_acquire);

    // Write placeholder size header (will be updated in commit_write)
    uint32_t placeholder = 0;
    std::memcpy(data_region_ + write_idx, &placeholder, sizeof(uint32_t));

    // Store the position where we wrote the size header
    result.write_idx = write_idx;

    // Advance past size header
    write_idx = (write_idx + sizeof(uint32_t)) % header_->buffer_size;

    // Return pointer to data area (with mirrored mapping, this is safe for any write)
    result.data = data_region_ + write_idx;
    // Return the FULL available space, not just what was requested!
    result.max_size = max_data_size;
    result.valid = true;

    return result;
}

void LockFreeRingBuffer::commit_write(const WriteReservation& reservation, size_t actual_size) {
    if (!reservation.valid) {
        std::cerr << "LockFreeRingBuffer::commit_write: invalid reservation" << std::endl;
        return;
    }

    if (actual_size > reservation.max_size) {
        std::cerr << "LockFreeRingBuffer::commit_write: actual_size " << actual_size 
                  << " exceeds reserved " << reservation.max_size << std::endl;
        return;
    }

    // Write actual size to the header position
    uint32_t size32 = static_cast<uint32_t>(actual_size);
    std::memcpy(data_region_ + reservation.write_idx, &size32, sizeof(uint32_t));

    // Calculate new write index
    size_t data_start = (reservation.write_idx + sizeof(uint32_t)) % header_->buffer_size;
    size_t new_write_idx = (data_start + actual_size) % header_->buffer_size;

    // Update write index with release semantics
    header_->write_idx.store(new_write_idx, std::memory_order_release);

    // Notify waiting readers
    {
        boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex> 
            lock(header_->mutex);
        header_->data_available.notify_one();
    }
}

LockFreeRingBuffer::ReadView LockFreeRingBuffer::try_read_view() {
    ReadView result{nullptr, 0, 0, false};

    // Load indices with acquire semantics
    size_t read_idx = header_->read_idx.load(std::memory_order_acquire);
    size_t write_idx = header_->write_idx.load(std::memory_order_acquire);

    // Check if buffer is empty
    if (read_idx == write_idx) {
        return result; // Empty
    }

    // Read size header - with mirrored mapping, no wrap-around needed!
    uint32_t message_size = 0;
    std::memcpy(&message_size, data_region_ + read_idx, sizeof(uint32_t));
    size_t data_start = (read_idx + sizeof(uint32_t)) % header_->buffer_size;

    // Validate size
    if (message_size > header_->max_message_size) {
        std::cerr << "Corrupt message size in read_view: " << message_size << std::endl;
        return result;
    }

    // Return pointer directly into the ring buffer
    // With mirrored mapping, this is always a valid contiguous view
    result.data = data_region_ + data_start;
    result.size = message_size;
    result.read_idx = (data_start + message_size) % header_->buffer_size;
    result.valid = true;

    return result;
}

void LockFreeRingBuffer::commit_read(const ReadView& view) {
    if (!view.valid) {
        std::cerr << "LockFreeRingBuffer::commit_read: invalid view" << std::endl;
        return;
    }

    // Update read index with release semantics
    header_->read_idx.store(view.read_idx, std::memory_order_release);
}

} // namespace nprpc::impl
