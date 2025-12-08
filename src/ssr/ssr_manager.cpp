// Copyright (c) 2025 nikitapnn1@gmail.com
// SPDX-License-Identifier: MIT

#ifdef NPRPC_SSR_ENABLED

#include <nprpc/impl/ssr_manager.hpp>
#include <nprpc/impl/nprpc_impl.hpp>

#include <iostream>
#include <filesystem>

namespace nprpc::impl {

namespace fs = std::filesystem;

NPRPC_API std::unique_ptr<NodeWorkerManager> g_ssr_manager;

NPRPC_API void init_ssr(boost::asio::io_context& ioc) {
    if (!g_cfg.ssr_enabled) {
        return;
    }
    
    if (g_cfg.ssr_handler_dir.empty()) {
        std::cerr << "[SSR] Error: ssr_handler_dir must be set for SSR" << std::endl;
        return;
    }
    
    // The SvelteKit build output should be in ssr_handler_dir
    // The handler is at ssr_handler_dir/index.js
    fs::path handler_path = fs::path(g_cfg.ssr_handler_dir);
    fs::path index_js = handler_path / "index.js";
    
    if (!fs::exists(index_js)) {
        std::cerr << "[SSR] Error: Handler not found at " << index_js << std::endl;
        std::cerr << "[SSR] Make sure to build your SvelteKit app with @nprpc/adapter-sveltekit" << std::endl;
        return;
    }
    
    std::cout << "[SSR] Initializing Node.js worker..." << std::endl;
    std::cout << "[SSR] Handler path: " << handler_path << std::endl;
    
    g_ssr_manager = std::make_unique<NodeWorkerManager>(ioc);
    
    if (!g_ssr_manager->start(handler_path.string())) {
        std::cerr << "[SSR] Failed to start Node.js worker" << std::endl;
        g_ssr_manager.reset();
        return;
    }
    
    std::cout << "[SSR] Node.js worker started successfully" << std::endl;
    std::cout << "[SSR] Channel ID: " << g_ssr_manager->channel_id() << std::endl;
}

NPRPC_API void stop_ssr() {
    if (g_ssr_manager) {
        std::cout << "[SSR] Stopping Node.js worker..." << std::endl;
        g_ssr_manager->stop();
        g_ssr_manager.reset();
        std::cout << "[SSR] Node.js worker stopped" << std::endl;
    }
}

} // namespace nprpc::impl

#endif // NPRPC_SSR_ENABLED
