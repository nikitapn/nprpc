// Copyright (c) 2021-2025 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by
// LICENSING file in the topmost directory

#ifndef NPRPC_HTTP_FILE_CACHE_HPP
#define NPRPC_HTTP_FILE_CACHE_HPP

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <list>
#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace nprpc::impl {

/// Represents a cached file with zero-copy access.
/// Files are either heap-allocated (small) or memory-mapped (large).
class CachedFile
{
public:
  CachedFile() = default;
  ~CachedFile();

  // Non-copyable, movable
  CachedFile(const CachedFile&) = delete;
  CachedFile& operator=(const CachedFile&) = delete;
  CachedFile(CachedFile&& other) noexcept;
  CachedFile& operator=(CachedFile&& other) noexcept;

  /// Load file from disk. Returns false on failure.
  /// Uses mmap for files >= mmap_threshold bytes.
  bool load(const std::filesystem::path& path,
            size_t mmap_threshold = 64 * 1024);

  /// Direct access to file data (zero-copy)
  const uint8_t* data() const noexcept { return data_; }
  size_t size() const noexcept { return size_; }

  /// Pre-computed content type (points to static storage)
  std::string_view content_type() const noexcept { return content_type_; }

  /// File modification time for staleness checking
  std::filesystem::file_time_type mtime() const noexcept { return mtime_; }

  /// Memory usage for cache accounting
  size_t memory_usage() const noexcept;

  /// Reference counting for safe eviction.
  /// Callers must acquire() before using data and release() when done.
  /// Cache won't evict files with active_refs > 0.
  void acquire() const noexcept
  {
    active_refs_.fetch_add(1, std::memory_order_relaxed);
  }
  void release() const noexcept
  {
    active_refs_.fetch_sub(1, std::memory_order_release);
  }
  int32_t active_refs() const noexcept
  {
    return active_refs_.load(std::memory_order_acquire);
  }

private:
  void cleanup() noexcept;

  const uint8_t* data_ = nullptr;
  size_t size_ = 0;
  std::string_view content_type_;
  std::filesystem::file_time_type mtime_{};

  // For heap-allocated small files
  std::vector<uint8_t> heap_data_;

  // For mmap'd large files
  void* mmap_addr_ = nullptr;
  size_t mmap_len_ = 0;

  // Active transfer count - prevents eviction while > 0
  mutable std::atomic<int32_t> active_refs_{0};
};

/// RAII guard for CachedFile reference counting.
/// Automatically calls acquire() on construction and release() on destruction.
class CachedFileGuard
{
public:
  CachedFileGuard() = default;

  explicit CachedFileGuard(std::shared_ptr<const CachedFile> file)
      : file_(std::move(file))
  {
    if (file_) {
      file_->acquire();
    }
  }

  ~CachedFileGuard()
  {
    if (file_) {
      file_->release();
    }
  }

  // Movable
  CachedFileGuard(CachedFileGuard&& other) noexcept
      : file_(std::move(other.file_))
  {
    other.file_ = nullptr;
  }

  CachedFileGuard& operator=(CachedFileGuard&& other) noexcept
  {
    if (this != &other) {
      if (file_) {
        file_->release();
      }
      file_ = std::move(other.file_);
      other.file_ = nullptr;
    }
    return *this;
  }

  // Non-copyable
  CachedFileGuard(const CachedFileGuard&) = delete;
  CachedFileGuard& operator=(const CachedFileGuard&) = delete;

  const CachedFile* operator->() const noexcept { return file_.get(); }
  const CachedFile& operator*() const noexcept { return *file_; }
  explicit operator bool() const noexcept { return file_ != nullptr; }

  const std::shared_ptr<const CachedFile>& get() const noexcept
  {
    return file_;
  }

private:
  std::shared_ptr<const CachedFile> file_;
};

/// Configuration for HttpFileCache
struct HttpFileCacheConfig {
  size_t max_memory_bytes = 64 * 1024 * 1024; // 64MB
  size_t max_entries = 1000;
  size_t mmap_threshold = 64 * 1024; // 64KB
  std::chrono::seconds mtime_check_interval{
      60}; // Re-check file mtime every 60s
};

/// Thread-safe LRU file cache with zero-copy access.
/// Files with active transfers (active_refs > 0) are protected from eviction.
class HttpFileCache
{
public:
  using Config = HttpFileCacheConfig;

  explicit HttpFileCache(Config config = {});
  ~HttpFileCache() = default;

  // Non-copyable, non-movable (contains mutex)
  HttpFileCache(const HttpFileCache&) = delete;
  HttpFileCache& operator=(const HttpFileCache&) = delete;

  /// Get a file from cache or load from disk.
  /// Returns a guard that keeps the file pinned until destroyed.
  /// Returns empty guard if file doesn't exist or can't be loaded.
  CachedFileGuard get(const std::filesystem::path& path);

  /// Get a file only if already cached (no disk I/O).
  /// Returns empty guard if not in cache.
  CachedFileGuard get_if_cached(const std::filesystem::path& path);

  /// Explicitly insert a file into cache.
  /// Useful for pre-warming or custom content.
  void put(const std::filesystem::path& path, std::shared_ptr<CachedFile> file);

  /// Remove a specific file from cache (waits for active transfers).
  /// Returns true if file was in cache, false otherwise.
  bool invalidate(const std::filesystem::path& path);

  /// Clear entire cache (waits for active transfers).
  void clear();

  /// Statistics
  struct Stats {
    size_t entries = 0;
    size_t memory_bytes = 0;
    uint64_t hits = 0;
    uint64_t misses = 0;
    uint64_t evictions = 0;
    uint64_t eviction_skips = 0; // Skipped due to active transfers
  };
  Stats stats() const;

  /// Update configuration (may trigger eviction)
  void set_max_memory(size_t bytes);
  void set_max_entries(size_t count);

private:
  struct CacheEntry {
    std::shared_ptr<CachedFile> file;
    std::list<std::string>::iterator lru_iter;
    std::chrono::steady_clock::time_point last_mtime_check;
  };

  // Must hold write lock
  void evict_if_needed();
  void touch_lru(CacheEntry& entry);
  bool is_stale(CacheEntry& entry, const std::filesystem::path& path);

  Config config_;

  mutable std::shared_mutex mutex_;
  std::unordered_map<std::string, CacheEntry> cache_;
  std::list<std::string> lru_order_; // Front = most recent, back = oldest

  size_t current_memory_ = 0;

  mutable std::atomic<uint64_t> hits_{0};
  mutable std::atomic<uint64_t> misses_{0};
  mutable std::atomic<uint64_t> evictions_{0};
  mutable std::atomic<uint64_t> eviction_skips_{0};
};

/// Global file cache instance (initialized by Rpc)
HttpFileCache& get_file_cache();
void init_file_cache(HttpFileCacheConfig config = {});

} // namespace nprpc::impl

#endif // NPRPC_HTTP_FILE_CACHE_HPP
