// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <iostream>
#include <nprpc/nprpc.hpp>

#include <boost/asio/signal_set.hpp>

int main(int argc, char* argv[])
{
  // Using a single io_context for simplicity
  boost::asio::io_context ioc;

  try {
    auto rpc =
        nprpc::RpcBuilder()
            .set_log_level(nprpc::LogLevel::trace)
            .with_hostname("localhost")
            .with_http(3000)
            .root_dir("/home/nikita/projects/nprpc/examples/ssr-svelte-app/"
                      "client/build/"
                      "client")
            .ssl("/home/nikita/projects/nprpc/certs/out/localhost.crt",
                 "/home/nikita/projects/nprpc/certs/out/localhost.key")
            .enable_http3()
            .enable_ssr("/home/nikita/projects/nprpc/examples/"
                        "ssr-svelte-app/client/build")
            .build();

    // Set up signal handling to allow clean shutdown on SIGINT and SIGTERM
    boost::asio::signal_set signals(rpc->ioc(), SIGINT, SIGTERM);
    signals.async_wait(
        [&](boost::system::error_code const&, int) { rpc->ioc().stop(); });

    std::cout << "Starting SSR Svelte app server on https://linuxvm:3000"
              << std::endl;
    rpc->run();
    rpc->destroy();
  } catch (const nprpc::Exception& ex) {
    std::cerr << "Error starting server: " << ex.what() << "\n";
    return EXIT_FAILURE;
  }



  return EXIT_SUCCESS;
}