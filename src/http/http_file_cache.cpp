// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <nprpc/impl/http_file_cache.hpp>
#include <nprpc/impl/http_utils.hpp>

#include <cassert>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace nprpc::impl {

//==============================================================================
// CachedFile Implementation
//==============================================================================

CachedFile::~CachedFile() { cleanup(); }

CachedFile::CachedFile(CachedFile&& other) noexcept
    : data_(other.data_)
    , size_(other.size_)
    , content_type_(other.content_type_)
    , mtime_(other.mtime_)
    , heap_data_(std::move(other.heap_data_))
    , mmap_addr_(other.mmap_addr_)
    , mmap_len_(other.mmap_len_)
    , active_refs_(other.active_refs_.load(std::memory_order_relaxed))
{
  other.data_ = nullptr;
  other.size_ = 0;
  other.mmap_addr_ = nullptr;
  other.mmap_len_ = 0;
}

CachedFile& CachedFile::operator=(CachedFile&& other) noexcept
{
  if (this != &other) {
    cleanup();

    data_ = other.data_;
    size_ = other.size_;
    content_type_ = other.content_type_;
    mtime_ = other.mtime_;
    heap_data_ = std::move(other.heap_data_);
    mmap_addr_ = other.mmap_addr_;
    mmap_len_ = other.mmap_len_;
    active_refs_.store(other.active_refs_.load(std::memory_order_relaxed),
                       std::memory_order_relaxed);

    other.data_ = nullptr;
    other.size_ = 0;
    other.mmap_addr_ = nullptr;
    other.mmap_len_ = 0;
  }
  return *this;
}

void CachedFile::cleanup() noexcept
{
  if (mmap_addr_) {
#ifdef _WIN32
    UnmapViewOfFile(mmap_addr_);
#else
    munmap(mmap_addr_, mmap_len_);
#endif
    mmap_addr_ = nullptr;
    mmap_len_ = 0;
  }
  heap_data_.clear();
  heap_data_.shrink_to_fit();
  data_ = nullptr;
  size_ = 0;
}

bool CachedFile::load(const std::filesystem::path& path, size_t mmap_threshold)
{
  cleanup();

  std::error_code ec;
  auto file_size = std::filesystem::file_size(path, ec);
  if (ec) {
    return false;
  }

  mtime_ = std::filesystem::last_write_time(path, ec);
  if (ec) {
    return false;
  }

  content_type_ = mime_type(path.string());

  if (file_size == 0) {
    // Empty file - just set size to 0, data_ remains nullptr
    size_ = 0;
    return true;
  }

  // Use mmap for large files
  if (file_size >= mmap_threshold) {
#ifdef _WIN32
    HANDLE hFile =
        CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (hFile == INVALID_HANDLE_VALUE) {
      return false;
    }

    HANDLE hMapping =
        CreateFileMappingW(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);

    if (!hMapping) {
      CloseHandle(hFile);
      return false;
    }

    mmap_addr_ = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0,
                               0 // Map entire file
    );

    CloseHandle(hMapping);
    CloseHandle(hFile);

    if (!mmap_addr_) {
      return false;
    }

    mmap_len_ = static_cast<size_t>(file_size);
    data_ = static_cast<const uint8_t*>(mmap_addr_);
    size_ = mmap_len_;
#else
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
      return false;
    }

    mmap_addr_ = mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    if (mmap_addr_ == MAP_FAILED) {
      mmap_addr_ = nullptr;
      return false;
    }

    // Hint to kernel: we'll read sequentially
    madvise(mmap_addr_, file_size, MADV_SEQUENTIAL);

    mmap_len_ = file_size;
    data_ = static_cast<const uint8_t*>(mmap_addr_);
    size_ = mmap_len_;
#endif
  } else {
    // Small file: read into heap
    std::ifstream file(path, std::ios::binary);
    if (!file) {
      return false;
    }

    heap_data_.resize(file_size);
    file.read(reinterpret_cast<char*>(heap_data_.data()),
              static_cast<std::streamsize>(file_size));

    if (!file) {
      heap_data_.clear();
      return false;
    }

    data_ = heap_data_.data();
    size_ = heap_data_.size();
  }

  return true;
}

size_t CachedFile::memory_usage() const noexcept
{
  if (mmap_addr_) {
    // mmap'd files don't count against heap memory,
    // but we track them for cache limit purposes
    return mmap_len_;
  }
  return heap_data_.capacity();
}

//==============================================================================
// HttpFileCache Implementation
//==============================================================================

HttpFileCache::HttpFileCache(Config config)
    : config_(std::move(config))
{
}

CachedFileGuard HttpFileCache::get(const std::filesystem::path& path)
{
  auto canonical_path = path.lexically_normal().string();

  // Try read-lock first (fast path for cache hit)
  {
    std::shared_lock lock(mutex_);
    auto it = cache_.find(canonical_path);
    if (it != cache_.end()) {
      auto& entry = it->second;

      // Check if file is stale (without lock upgrade - just return cached
      // version) Staleness is checked with timestamp, actual reload
      // happens on next access
      if (!is_stale(entry, path)) {
        hits_.fetch_add(1, std::memory_order_relaxed);
        return CachedFileGuard(entry.file);
      }
    }
  }

  // Cache miss or stale - need write lock to load/update
  std::unique_lock lock(mutex_);

  // Double-check after acquiring write lock
  auto it = cache_.find(canonical_path);
  if (it != cache_.end() && !is_stale(it->second, path)) {
    hits_.fetch_add(1, std::memory_order_relaxed);
    touch_lru(it->second);
    return CachedFileGuard(it->second.file);
  }

  misses_.fetch_add(1, std::memory_order_relaxed);

  // Load file from disk
  auto file = std::make_shared<CachedFile>();
  if (!file->load(path, config_.mmap_threshold)) {
    return CachedFileGuard(); // File doesn't exist or can't be read
  }

  // If entry exists but is stale, update it
  if (it != cache_.end()) {
    current_memory_ -= it->second.file->memory_usage();
    it->second.file = file;
    it->second.last_mtime_check = std::chrono::steady_clock::now();
    current_memory_ += file->memory_usage();
    touch_lru(it->second);
  } else {
    // New entry
    evict_if_needed();

    lru_order_.push_front(canonical_path);
    CacheEntry entry{.file = file,
                     .lru_iter = lru_order_.begin(),
                     .last_mtime_check = std::chrono::steady_clock::now()};
    cache_.emplace(canonical_path, std::move(entry));
    current_memory_ += file->memory_usage();
  }

  return CachedFileGuard(file);
}

CachedFileGuard HttpFileCache::get_if_cached(const std::filesystem::path& path)
{
  auto canonical_path = path.lexically_normal().string();

  std::shared_lock lock(mutex_);
  auto it = cache_.find(canonical_path);
  if (it != cache_.end()) {
    hits_.fetch_add(1, std::memory_order_relaxed);
    return CachedFileGuard(it->second.file);
  }

  misses_.fetch_add(1, std::memory_order_relaxed);
  return CachedFileGuard();
}

void HttpFileCache::put(const std::filesystem::path& path,
                        std::shared_ptr<CachedFile> file)
{
  auto canonical_path = path.lexically_normal().string();

  std::unique_lock lock(mutex_);

  auto it = cache_.find(canonical_path);
  if (it != cache_.end()) {
    // Update existing
    current_memory_ -= it->second.file->memory_usage();
    it->second.file = std::move(file);
    it->second.last_mtime_check = std::chrono::steady_clock::now();
    current_memory_ += it->second.file->memory_usage();
    touch_lru(it->second);
  } else {
    // Insert new
    evict_if_needed();

    lru_order_.push_front(canonical_path);
    CacheEntry entry{.file = std::move(file),
                     .lru_iter = lru_order_.begin(),
                     .last_mtime_check = std::chrono::steady_clock::now()};
    current_memory_ += entry.file->memory_usage();
    cache_.emplace(canonical_path, std::move(entry));
  }
}

bool HttpFileCache::invalidate(const std::filesystem::path& path)
{
  auto canonical_path = path.lexically_normal().string();

  std::unique_lock lock(mutex_);
  auto it = cache_.find(canonical_path);
  if (it == cache_.end()) {
    return false;
  }

  // Wait for active transfers to complete
  // In practice, this should be rare and brief
  while (it->second.file->active_refs() > 0) {
    lock.unlock();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    lock.lock();
    it = cache_.find(canonical_path);
    if (it == cache_.end()) {
      return false; // Someone else removed it
    }
  }

  current_memory_ -= it->second.file->memory_usage();
  lru_order_.erase(it->second.lru_iter);
  cache_.erase(it);

  return true;
}

void HttpFileCache::clear()
{
  std::unique_lock lock(mutex_);

  // Wait for all active transfers
  for (auto& [path, entry] : cache_) {
    while (entry.file->active_refs() > 0) {
      lock.unlock();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      lock.lock();
    }
  }

  cache_.clear();
  lru_order_.clear();
  current_memory_ = 0;
}

HttpFileCache::Stats HttpFileCache::stats() const
{
  std::shared_lock lock(mutex_);
  return Stats{.entries = cache_.size(),
               .memory_bytes = current_memory_,
               .hits = hits_.load(std::memory_order_relaxed),
               .misses = misses_.load(std::memory_order_relaxed),
               .evictions = evictions_.load(std::memory_order_relaxed),
               .eviction_skips =
                   eviction_skips_.load(std::memory_order_relaxed)};
}

void HttpFileCache::set_max_memory(size_t bytes)
{
  std::unique_lock lock(mutex_);
  config_.max_memory_bytes = bytes;
  evict_if_needed();
}

void HttpFileCache::set_max_entries(size_t count)
{
  std::unique_lock lock(mutex_);
  config_.max_entries = count;
  evict_if_needed();
}

void HttpFileCache::evict_if_needed()
{
  // Called with write lock held

  // Evict until we're under limits
  while (!lru_order_.empty() && (cache_.size() > config_.max_entries ||
                                 current_memory_ > config_.max_memory_bytes)) {
    // Find oldest entry that isn't being actively used
    bool evicted = false;
    for (auto rit = lru_order_.rbegin(); rit != lru_order_.rend(); ++rit) {
      auto it = cache_.find(*rit);
      if (it == cache_.end()) {
        continue; // Shouldn't happen, but be safe
      }

      if (it->second.file->active_refs() > 0) {
        // File is being transferred, skip it
        eviction_skips_.fetch_add(1, std::memory_order_relaxed);
        continue;
      }

      // Evict this entry
      current_memory_ -= it->second.file->memory_usage();
      auto lru_iter = it->second.lru_iter;
      cache_.erase(it);
      lru_order_.erase(lru_iter);
      evictions_.fetch_add(1, std::memory_order_relaxed);
      evicted = true;
      break;
    }

    if (!evicted) {
      // All entries are being actively used, can't evict more
      break;
    }
  }
}

void HttpFileCache::touch_lru(CacheEntry& entry)
{
  // Move to front of LRU list
  lru_order_.splice(lru_order_.begin(), lru_order_, entry.lru_iter);
}

bool HttpFileCache::is_stale(CacheEntry& entry,
                             const std::filesystem::path& path)
{
  auto now = std::chrono::steady_clock::now();

  // Only check mtime periodically to avoid stat() on every request
  if (now - entry.last_mtime_check < config_.mtime_check_interval) {
    return false;
  }

  entry.last_mtime_check = now;

  std::error_code ec;
  auto current_mtime = std::filesystem::last_write_time(path, ec);
  if (ec) {
    // File was deleted or inaccessible
    return true;
  }

  return current_mtime != entry.file->mtime();
}

//==============================================================================
// Global Instance
//==============================================================================

namespace {
std::unique_ptr<HttpFileCache> g_file_cache;
std::once_flag g_file_cache_init_flag;
} // namespace

HttpFileCache& get_file_cache()
{
  std::call_once(g_file_cache_init_flag,
                 []() { g_file_cache = std::make_unique<HttpFileCache>(); });
  return *g_file_cache;
}

void init_file_cache(HttpFileCache::Config config)
{
  std::call_once(g_file_cache_init_flag, [&config]() {
    g_file_cache = std::make_unique<HttpFileCache>(std::move(config));
  });
}

} // namespace nprpc::impl
