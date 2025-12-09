// Copyright (c) 2025 nikitapnn1@gmail.com
// SPDX-License-Identifier: MIT

#ifdef NPRPC_SSR_ENABLED

#include "../logging.hpp"
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/ssr_manager.hpp>

#include <filesystem>

namespace nprpc::impl {

namespace fs = std::filesystem;

NPRPC_API std::unique_ptr<NodeWorkerManager> g_ssr_manager;

NPRPC_API void init_ssr(boost::asio::io_context& ioc)
{
  if (!g_cfg.ssr_enabled) {
    return;
  }

  if (g_cfg.ssr_handler_dir.empty()) {
    NPRPC_LOG_ERROR("[SSR] Error: ssr_handler_dir must be set for SSR");
    return;
  }

  // The SvelteKit build output should be in ssr_handler_dir
  // The handler is at ssr_handler_dir/index.js
  fs::path handler_path = fs::path(g_cfg.ssr_handler_dir);
  fs::path index_js = handler_path / "index.js";

  if (!fs::exists(index_js)) {
    NPRPC_LOG_ERROR("[SSR] Error: Handler not found at {}", index_js.string());
    NPRPC_LOG_ERROR("[SSR] Make sure to build your SvelteKit app with "
                    "@nprpc/adapter-sveltekit");
    return;
  }

  NPRPC_LOG_INFO("[SSR] Initializing Node.js worker...");
  NPRPC_LOG_INFO("[SSR] Handler path: {}", handler_path.string());

  g_ssr_manager = std::make_unique<NodeWorkerManager>(ioc);

  if (!g_ssr_manager->start(handler_path.string())) {
    NPRPC_LOG_ERROR("[SSR] Failed to start Node.js worker");
    g_ssr_manager.reset();
    return;
  }

  NPRPC_LOG_INFO("[SSR] Node.js worker started successfully");
  NPRPC_LOG_INFO("[SSR] Channel ID: {}", g_ssr_manager->channel_id());
}

NPRPC_API void stop_ssr()
{
  if (g_ssr_manager) {
    NPRPC_LOG_INFO("[SSR] Stopping Node.js worker...");
    g_ssr_manager->stop();
    g_ssr_manager.reset();
    NPRPC_LOG_INFO("[SSR] Node.js worker stopped");
  }
}

} // namespace nprpc::impl

#endif // NPRPC_SSR_ENABLED
