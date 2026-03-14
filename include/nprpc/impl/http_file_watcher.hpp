// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <filesystem>
#include <string>
#ifdef __linux__
#include <thread>
#include <functional>
#endif

namespace nprpc::impl {

/// Watches a directory tree for file changes and immediately invalidates the
/// corresponding entries in HttpFileCache.  Meant for development use:
/// set config.watch_files = true in RpcBuilder to enable it.
///
/// Linux: uses inotify (recursive, via IN_CLOSE_WRITE / IN_MOVED_TO /
///        IN_CREATE / IN_DELETE and IN_CREATE+IN_ISDIR for new subdirs).
/// Other platforms: no-op stub (mtime polling already handles staleness).
class HttpFileWatcher
{
public:
  /// Start watching `client_root` recursively for HTTP cache invalidation.
  /// If `ssr_server_root` is non-empty its tree is also watched; when any
  /// file there changes `on_server_rebuilt` is called after a 500 ms quiet
  /// period (debounce) so that mid-build partial states are never exposed.
  explicit HttpFileWatcher(
      std::filesystem::path client_root,
      std::filesystem::path ssr_server_root = {},
      std::function<void()> on_server_rebuilt = {});

  /// Stop the background thread and release inotify resources.
  ~HttpFileWatcher();

  HttpFileWatcher(const HttpFileWatcher&) = delete;
  HttpFileWatcher& operator=(const HttpFileWatcher&) = delete;

private:
#ifdef __linux__
  void run();

  std::filesystem::path root_;             ///< client static root
  std::filesystem::path ssr_server_root_;  ///< server SSR build root (may be empty)
  std::function<void()> on_server_rebuilt_;
  int inotify_fd_ = -1;
  int stop_fd_    = -1; // eventfd used to unblock the poll loop
  std::thread thread_;
#endif
};

/// Start a global file watcher.
/// `client_root`       — directory tree watched for HTTP cache invalidation.
/// `ssr_server_root`   — optional additional tree; when any file there
///                       changes `on_server_rebuilt` is invoked after a
///                       500 ms debounce so the SSR Node.js process can
///                       be restarted after a full build.
/// Safe to call multiple times; only the first call has effect.
void start_file_watcher(
    const std::filesystem::path& client_root,
    const std::filesystem::path& ssr_server_root = {},
    std::function<void()>        on_server_rebuilt = {});

/// Stop the global file watcher (called from RpcImpl::destroy).
void stop_file_watcher();

} // namespace nprpc::impl
